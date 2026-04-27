#include "test_client/test_client_cli.h"

#include <stdexcept>
#include <string>

namespace mqtt {
namespace {

[[nodiscard]] bool has_more_arguments(const int index, const int argc) {
  return index + 1 < argc;
}

[[nodiscard]] std::string require_value(const int index, const int argc,
                                        const char *argv[],
                                        const std::string &option_name) {
  if (!has_more_arguments(index, argc)) {
    throw std::invalid_argument("Missing value for option: " + option_name);
  }
  return std::string(argv[index + 1]);
}

void parse_common_options(TestClientCliOptions &options, const int argc,
                          const char *argv[], int start_index) {
  for (int index = start_index; index < argc; ++index) {
    const std::string option_name = argv[index];

    if (option_name == "--profile") {
      options.profile_path =
          require_value(index, argc, argv, "--profile");
      ++index;
      continue;
    }
    if (option_name == "--output") {
      options.output_path = require_value(index, argc, argv, "--output");
      ++index;
      continue;
    }

    auto add_override = [&options, &index, argc,
                         argv](const std::string &key_name,
                               const std::string &flag_name) {
      const std::string value = require_value(index, argc, argv, flag_name);
      options.overrides.emplace_back(key_name, value);
      ++index;
    };

    if (option_name == "--host") {
      add_override("host", "--host");
      continue;
    }
    if (option_name == "--port") {
      add_override("port", "--port");
      continue;
    }
    if (option_name == "--transport") {
      add_override("transport", "--transport");
      continue;
    }
    if (option_name == "--ws-path") {
      add_override("ws_path", "--ws-path");
      continue;
    }
    if (option_name == "--ws-header") {
      add_override("ws_header", "--ws-header");
      continue;
    }
    if (option_name == "--client-id") {
      add_override("client_id", "--client-id");
      continue;
    }
    if (option_name == "--clean-start") {
      add_override("clean_start", "--clean-start");
      continue;
    }
    if (option_name == "--keep-alive-seconds") {
      add_override("keep_alive_seconds", "--keep-alive-seconds");
      continue;
    }
    if (option_name == "--username") {
      add_override("username", "--username");
      continue;
    }
    if (option_name == "--password") {
      add_override("password", "--password");
      continue;
    }
    if (option_name == "--reconnect-period-ms") {
      add_override("reconnect_period_ms", "--reconnect-period-ms");
      continue;
    }
    if (option_name == "--maximum-reconnect-times") {
      add_override("maximum_reconnect_times", "--maximum-reconnect-times");
      continue;
    }

    throw std::invalid_argument("Unknown option: " + option_name);
  }
}

} // namespace

TestClientCliOptions parse_test_client_cli(const int argc, const char *argv[]) {
  TestClientCliOptions options;

  if (argc <= 1) {
    options.command = TestClientCommand::Help;
    return options;
  }

  const std::string command_name = argv[1];
  if (command_name == "--help" || command_name == "help") {
    options.command = TestClientCommand::Help;
    return options;
  }

  if (command_name == "connect") {
    options.command = TestClientCommand::Connect;
    parse_common_options(options, argc, argv, 2);
    return options;
  }

  if (command_name == "save-profile") {
    options.command = TestClientCommand::SaveProfile;
    parse_common_options(options, argc, argv, 2);
    if (options.output_path.empty()) {
      throw std::invalid_argument(
          "save-profile requires --output <profile-file>");
    }
    return options;
  }

  if (command_name == "show-profile") {
    options.command = TestClientCommand::ShowProfile;
    parse_common_options(options, argc, argv, 2);
    return options;
  }

  throw std::invalid_argument("Unknown command: " + command_name);
}

std::string test_client_help_text() {
  return
      "Usage:\n"
      "  yahatestclient <command> [options]\n\n"
      "Commands:\n"
      "  connect        Connect using profile + CLI overrides and keep session open\n"
      "  save-profile   Write profile file from defaults/profile/overrides\n"
      "  show-profile   Print effective profile\n"
      "  help           Show this help\n\n"
      "Common options:\n"
      "  --profile <file>\n"
      "  --host <host>\n"
      "  --port <port>\n"
      "  --transport <mqtt|ws>\n"
      "  --ws-path <path>\n"
      "  --ws-header <header-line>   (repeatable)\n"
      "  --client-id <id>\n"
      "  --clean-start <true|false>\n"
      "  --keep-alive-seconds <seconds>\n"
      "  --username <name>\n"
      "  --password <secret>\n"
      "  --reconnect-period-ms <milliseconds>\n"
      "  --maximum-reconnect-times <count>\n\n"
      "save-profile options:\n"
      "  --output <file>\n";
}

} // namespace mqtt
