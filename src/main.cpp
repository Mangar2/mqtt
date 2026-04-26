#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "broker/broker_error.h"
#include "broker/config_loader.h"
#include "monitoring/trace_level.h"

namespace {
constexpr std::string_view k_version = "0.1.0";

struct CliOptions {
  bool show_help = false;
  bool run_as_daemon = false;
  bool quiet = false;
  bool verbose = false;
  bool test_config_only = false;

  std::optional<std::filesystem::path> config_path;
  std::optional<uint16_t> port_override;
  std::optional<mqtt::TraceLevel> trace_level_override;

  bool trace_modules_seen = false;
  std::vector<std::string> trace_modules_override;
};

[[nodiscard]] bool has_help_flag(int argument_count, char *argument_values[]) {
  for (int argument_index = 1; argument_index < argument_count;
       ++argument_index) {
    const std::string_view argument = argument_values[argument_index];
    if (argument == "--help" || argument == "-h") {
      return true;
    }
  }

  return false;
}

[[nodiscard]] std::optional<uint16_t> parse_cli_port(std::string_view value) {
  if (value.empty()) {
    return std::nullopt;
  }

  uint32_t parsed_value = 0U;
  for (const char digit_character : value) {
    if (digit_character < '0' || digit_character > '9') {
      return std::nullopt;
    }

    parsed_value =
        (parsed_value * 10U) + static_cast<uint32_t>(digit_character - '0');
    if (parsed_value > 65535U) {
      return std::nullopt;
    }
  }

  return static_cast<uint16_t>(parsed_value);
}

void print_help(std::string_view executable_name) {
  std::cout << "Usage:\n"
            << "  " << executable_name << " [<config-path>] [options]\n"
            << "  " << executable_name << " [-c <config-file>] [options]\n\n"
            << "Options:\n"
            << "  -h, --help                           Show this help and exit.\n"
            << "  -c, --config-file <path>             Load configuration from file.\n"
            << "  -d, --daemon                         Run in background (POSIX).\n"
            << "  -p, --port <0..65535>                Override MQTT listener port.\n"
            << "      --port=<0..65535>                Override MQTT listener port.\n"
            << "  -q, --quiet                          Disable structured tracing output.\n"
            << "  -v, --verbose                        Set structured tracing to trace.\n"
            << "      --test-config                    Validate config and exit.\n"
            << "  --trace-level=<none|error|warning|info|trace>\n"
            << "                                       Override global tracing level.\n"
            << "  --trace-module=<module>              Repeatable module trace override.\n\n"
            << "Argument rules:\n"
            << "  <config-path> is optional and must be the first argument when provided.\n"
            << "  If both <config-path> and -c/--config-file are given, startup fails.\n"
            << "  Unknown options cause startup failure.\n";
}

[[nodiscard]] bool is_cli_flag(std::string_view argument) {
  return !argument.empty() && argument.front() == '-';
}

[[nodiscard]] std::optional<std::string_view>
split_value_after_equals(std::string_view argument,
                         std::string_view option_prefix) {
  if (!argument.starts_with(option_prefix)) {
    return std::nullopt;
  }

  const std::string_view raw_value = argument.substr(option_prefix.size());
  if (raw_value.empty()) {
    return std::nullopt;
  }

  return raw_value;
}

[[nodiscard]] bool consume_next_value(int argument_count, char *argument_values[],
                                      int &argument_index,
                                      std::string_view option_name,
                                      std::string_view &value,
                                      std::string &error_message) {
  if ((argument_index + 1) >= argument_count) {
    error_message = std::string("Missing value for ") + std::string(option_name);
    return false;
  }

  ++argument_index;
  value = argument_values[argument_index];
  return true;
}

[[nodiscard]] bool parse_cli_arguments(int argument_count,
                                       char *argument_values[],
                                       CliOptions &options,
                                       std::string &error_message) {
  const bool has_positional_candidate =
      argument_count >= 2 && !is_cli_flag(std::string_view(argument_values[1]));

  for (int argument_index = 1; argument_index < argument_count;
       ++argument_index) {
    const std::string_view argument = argument_values[argument_index];

    if (argument == "--help" || argument == "-h") {
      options.show_help = true;
      continue;
    }

    if (argument_index == 1 && has_positional_candidate) {
      if (options.config_path.has_value()) {
        error_message = "Configuration file specified more than once.";
        return false;
      }
      options.config_path = std::filesystem::path(argument);
      continue;
    }

    if (argument == "-c" || argument == "--config-file") {
      std::string_view config_value;
      if (!consume_next_value(argument_count, argument_values, argument_index,
                              argument, config_value, error_message)) {
        return false;
      }
      if (options.config_path.has_value()) {
        error_message = "Configuration file specified more than once.";
        return false;
      }
      options.config_path = std::filesystem::path(config_value);
      continue;
    }

    if (argument.starts_with("--config-file=")) {
      const std::optional<std::string_view> config_value =
          split_value_after_equals(argument, "--config-file=");
      if (!config_value.has_value()) {
        error_message = "Missing value for --config-file";
        return false;
      }
      if (options.config_path.has_value()) {
        error_message = "Configuration file specified more than once.";
        return false;
      }
      options.config_path = std::filesystem::path(*config_value);
      continue;
    }

    if (argument == "-d" || argument == "--daemon") {
      options.run_as_daemon = true;
      continue;
    }

    if (argument == "-q" || argument == "--quiet") {
      options.quiet = true;
      continue;
    }

    if (argument == "-v" || argument == "--verbose") {
      options.verbose = true;
      continue;
    }

    if (argument == "--test-config") {
      options.test_config_only = true;
      continue;
    }

    if (argument == "-p" || argument == "--port") {
      std::string_view port_value;
      if (!consume_next_value(argument_count, argument_values, argument_index,
                              argument, port_value, error_message)) {
        return false;
      }
      const std::optional<uint16_t> parsed_port = parse_cli_port(port_value);
      if (!parsed_port.has_value()) {
        error_message = std::string("Invalid --port value: ") +
                        std::string(port_value);
        return false;
      }
      options.port_override = *parsed_port;
      continue;
    }

    if (argument.starts_with("--port=")) {
      const std::optional<std::string_view> port_value =
          split_value_after_equals(argument, "--port=");
      if (!port_value.has_value()) {
        error_message = "Missing value for --port";
        return false;
      }
      const std::optional<uint16_t> parsed_port = parse_cli_port(*port_value);
      if (!parsed_port.has_value()) {
        error_message = std::string("Invalid --port value: ") +
                        std::string(*port_value);
        return false;
      }
      options.port_override = *parsed_port;
      continue;
    }

    if (argument == "--trace-level") {
      std::string_view level_value;
      if (!consume_next_value(argument_count, argument_values, argument_index,
                              argument, level_value, error_message)) {
        return false;
      }
      const std::optional<mqtt::TraceLevel> parsed_level =
          mqtt::parse_trace_level(level_value);
      if (!parsed_level.has_value()) {
        error_message = std::string("Invalid --trace-level value: ") +
                        std::string(level_value);
        return false;
      }
      options.trace_level_override = *parsed_level;
      continue;
    }

    if (argument.starts_with("--trace-level=")) {
      const std::optional<std::string_view> level_value =
          split_value_after_equals(argument, "--trace-level=");
      if (!level_value.has_value()) {
        error_message = "Missing value for --trace-level";
        return false;
      }
      const std::optional<mqtt::TraceLevel> parsed_level =
          mqtt::parse_trace_level(*level_value);
      if (!parsed_level.has_value()) {
        error_message = std::string("Invalid --trace-level value: ") +
                        std::string(*level_value);
        return false;
      }
      options.trace_level_override = *parsed_level;
      continue;
    }

    if (argument == "--trace-module") {
      std::string_view module_value;
      if (!consume_next_value(argument_count, argument_values, argument_index,
                              argument, module_value, error_message)) {
        return false;
      }
      options.trace_modules_seen = true;
      if (!module_value.empty()) {
        options.trace_modules_override.emplace_back(module_value);
      }
      continue;
    }

    if (argument.starts_with("--trace-module=")) {
      const std::optional<std::string_view> module_value =
          split_value_after_equals(argument, "--trace-module=");
      if (!module_value.has_value()) {
        error_message = "Missing value for --trace-module";
        return false;
      }
      options.trace_modules_seen = true;
      if (!module_value->empty()) {
        options.trace_modules_override.emplace_back(*module_value);
      }
      continue;
    }

    error_message = std::string("Unknown argument: ") + std::string(argument);
    return false;
  }

  return true;
}

void apply_cli_overrides(const CliOptions &options, mqtt::BrokerConfig &config) {
  if (options.port_override.has_value()) {
    config.mqtt_port = *options.port_override;
  }

  if (options.trace_level_override.has_value()) {
    config.trace_global_level = *options.trace_level_override;
  }

  if (options.trace_modules_seen) {
    config.trace_modules = options.trace_modules_override;
  }

  if (options.verbose) {
    config.trace_global_level = mqtt::TraceLevel::Trace;
  }

  if (options.quiet) {
    config.trace_global_level = mqtt::TraceLevel::None;
  }
}

[[nodiscard]] bool daemonize_process() {
#if defined(__unix__) || defined(__APPLE__)
  const pid_t first_fork = fork();
  if (first_fork < 0) {
    return false;
  }
  if (first_fork > 0) {
    std::exit(EXIT_SUCCESS);
  }

  if (setsid() < 0) {
    return false;
  }

  const pid_t second_fork = fork();
  if (second_fork < 0) {
    return false;
  }
  if (second_fork > 0) {
    std::exit(EXIT_SUCCESS);
  }

  umask(0);
  if (chdir("/") != 0) {
    return false;
  }

  const int null_fd = open("/dev/null", O_RDWR);
  if (null_fd < 0) {
    return false;
  }

  if (dup2(null_fd, STDIN_FILENO) < 0 || dup2(null_fd, STDOUT_FILENO) < 0 ||
      dup2(null_fd, STDERR_FILENO) < 0) {
    close(null_fd);
    return false;
  }

  if (null_fd > STDERR_FILENO) {
    close(null_fd);
  }

  return true;
#else
  return false;
#endif
}
} // namespace

int main(int argc, char *argv[]) {
  if (has_help_flag(argc, argv)) {
    const std::string_view executable_name =
        argc > 0 ? std::string_view(argv[0]) : "mqtt-broker";
    print_help(executable_name);
    return EXIT_SUCCESS;
  }

  CliOptions cli_options;
  std::string parse_error;
  if (!parse_cli_arguments(argc, argv, cli_options, parse_error)) {
    std::cerr << parse_error << '\n';
    return EXIT_FAILURE;
  }

  if (!cli_options.quiet) {
    std::cout << "mqtt-broker " << k_version << '\n';
  }

  // Precedence is deterministic: defaults < config file < CLI.
  mqtt::BrokerConfig config;
  if (cli_options.config_path.has_value()) {
    try {
      config = mqtt::ConfigLoader::load(*cli_options.config_path);
    } catch (const mqtt::BrokerException &exc) {
      std::cerr << "Configuration error: " << exc.what() << '\n';
      return EXIT_FAILURE;
    }
  }

  apply_cli_overrides(cli_options, config);

  if (cli_options.test_config_only) {
    if (!cli_options.config_path.has_value()) {
      std::cerr << "--test-config requires a config file via <config-path> or -c/--config-file.\n";
      return EXIT_FAILURE;
    }
    if (!cli_options.quiet) {
      std::cout << "Configuration valid: " << cli_options.config_path->string()
                << '\n';
    }
    return EXIT_SUCCESS;
  }

  if (cli_options.run_as_daemon && !daemonize_process()) {
    std::cerr << "Failed to daemonize process.\n";
    return EXIT_FAILURE;
  }

  mqtt::Broker::install_signal_handlers();

  mqtt::Broker broker(config);
  try {
    broker.startup();
  } catch (const std::exception &exc) {
    std::cerr << "Startup failed: " << exc.what() << '\n';
    return EXIT_FAILURE;
  }

  if (!cli_options.quiet) {
    std::cout << "Broker running on MQTT port " << config.mqtt_port;
    if (config.ws_port != 0U) {
      std::cout << ", WebSocket port " << config.ws_port;
    }
    std::cout << '\n';
  }

  // Main loop — poll for shutdown signal and tick monitoring.
  // Accept loops run on background threads started by Broker::startup().
  while (!mqtt::Broker::shutdown_requested()) {
    broker.tick();
    std::this_thread::sleep_for(
        std::chrono::milliseconds(config.tick_interval_ms));
  }

  broker.shutdown();
  if (!cli_options.quiet) {
    std::cout << "Broker stopped.\n";
  }
  return EXIT_SUCCESS;
}
