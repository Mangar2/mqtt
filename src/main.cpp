#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "broker/broker_error.h"
#include "broker/config_loader.h"
#include "monitoring/trace_level.h"

namespace {
constexpr std::string_view k_version = "0.1.0";

[[nodiscard]] bool is_cli_flag(std::string_view argument) {
  return argument.starts_with("--");
}

[[nodiscard]] std::optional<std::filesystem::path>
find_config_path(int argument_count, char *argument_values[]) {
  if (argument_count < 2) {
    return std::nullopt;
  }

  const std::string_view first_argument = argument_values[1];
  if (is_cli_flag(first_argument)) {
    return std::nullopt;
  }

  return std::filesystem::path(first_argument);
}

[[nodiscard]] std::string_view argument_value(std::string_view argument,
                                              std::string_view prefix) {
  return argument.substr(prefix.size());
}

bool apply_cli_trace_overrides(int argument_count, char *argument_values[],
                               mqtt::BrokerConfig &config,
                               const bool has_config_path) {
  const int first_cli_index = has_config_path ? 2 : 1;
  bool trace_module_flag_seen = false;

  for (int argument_index = first_cli_index; argument_index < argument_count;
       ++argument_index) {
    const std::string_view argument = argument_values[argument_index];

    if (argument.starts_with("--trace-level=")) {
      const std::optional<mqtt::TraceLevel> parsed_level =
          mqtt::parse_trace_level(
              argument_value(argument, "--trace-level="));
      if (!parsed_level.has_value()) {
        std::cerr << "Invalid --trace-level value: "
                  << argument_value(argument, "--trace-level=") << '\n';
        return false;
      }
      config.trace_global_level = *parsed_level;
      continue;
    }

    if (argument.starts_with("--trace-module=")) {
      if (!trace_module_flag_seen) {
        config.trace_modules.clear();
        trace_module_flag_seen = true;
      }
      const std::string_view module_name =
          argument_value(argument, "--trace-module=");
      if (!module_name.empty()) {
        config.trace_modules.emplace_back(module_name);
      }
      continue;
    }

    std::cerr << "Unknown argument: " << argument << '\n';
    return false;
  }

  return true;
}
} // namespace

int main(int argc, char *argv[]) {
  std::cout << "mqtt-broker " << k_version << '\n';

  // Precedence is deterministic: defaults < config file < CLI.
  mqtt::BrokerConfig config;
  const std::optional<std::filesystem::path> config_path =
      find_config_path(argc, argv);
  if (config_path.has_value()) {
    try {
      config = mqtt::ConfigLoader::load(*config_path);
    } catch (const mqtt::BrokerException &exc) {
      std::cerr << "Configuration error: " << exc.what() << '\n';
      return EXIT_FAILURE;
    }
  }

  if (!apply_cli_trace_overrides(argc, argv, config, config_path.has_value())) {
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

  std::cout << "Broker running on MQTT port " << config.mqtt_port;
  if (config.ws_port != 0U) {
    std::cout << ", WebSocket port " << config.ws_port;
  }
  std::cout << '\n';

  // Main loop — poll for shutdown signal and tick monitoring.
  // Accept loops run on background threads started by Broker::startup().
  while (!mqtt::Broker::shutdown_requested()) {
    broker.tick();
    std::this_thread::sleep_for(
        std::chrono::milliseconds(config.tick_interval_ms));
  }

  broker.shutdown();
  std::cout << "Broker stopped.\n";
  return EXIT_SUCCESS;
}
