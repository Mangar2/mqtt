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
    if (option_name == "--session-expiry-interval-seconds") {
      add_override("session_expiry_interval_seconds",
                   "--session-expiry-interval-seconds");
      continue;
    }
    if (option_name == "--receive-maximum") {
      add_override("receive_maximum", "--receive-maximum");
      continue;
    }
    if (option_name == "--maximum-packet-size") {
      add_override("maximum_packet_size", "--maximum-packet-size");
      continue;
    }
    if (option_name == "--topic-alias-maximum") {
      add_override("topic_alias_maximum", "--topic-alias-maximum");
      continue;
    }
    if (option_name == "--request-response-information") {
      add_override("request_response_information",
                   "--request-response-information");
      continue;
    }
    if (option_name == "--request-problem-information") {
      add_override("request_problem_information",
                   "--request-problem-information");
      continue;
    }
    if (option_name == "--connect-user-property") {
      add_override("connect_user_property", "--connect-user-property");
      continue;
    }
    if (option_name == "--authentication-method") {
      add_override("authentication_method", "--authentication-method");
      continue;
    }
    if (option_name == "--authentication-data") {
      add_override("authentication_data", "--authentication-data");
      continue;
    }
    if (option_name == "--will-topic") {
      add_override("will_topic", "--will-topic");
      continue;
    }
    if (option_name == "--will-payload") {
      add_override("will_payload", "--will-payload");
      continue;
    }
    if (option_name == "--will-qos") {
      add_override("will_qos", "--will-qos");
      continue;
    }
    if (option_name == "--will-retain") {
      add_override("will_retain", "--will-retain");
      continue;
    }
    if (option_name == "--will-delay-interval-seconds") {
      add_override("will_delay_interval_seconds",
                   "--will-delay-interval-seconds");
      continue;
    }
    if (option_name == "--will-payload-format-indicator") {
      add_override("will_payload_format_indicator",
                   "--will-payload-format-indicator");
      continue;
    }
    if (option_name == "--will-message-expiry-interval-seconds") {
      add_override("will_message_expiry_interval_seconds",
                   "--will-message-expiry-interval-seconds");
      continue;
    }
    if (option_name == "--will-content-type") {
      add_override("will_content_type", "--will-content-type");
      continue;
    }
    if (option_name == "--will-response-topic") {
      add_override("will_response_topic", "--will-response-topic");
      continue;
    }
    if (option_name == "--will-correlation-data") {
      add_override("will_correlation_data", "--will-correlation-data");
      continue;
    }
    if (option_name == "--will-user-property") {
      add_override("will_user_property", "--will-user-property");
      continue;
    }
    if (option_name == "--topic") {
      add_override("publish_topic", "--topic");
      continue;
    }
    if (option_name == "--qos") {
      add_override("publish_qos", "--qos");
      continue;
    }
    if (option_name == "--retain") {
      add_override("publish_retain", "--retain");
      continue;
    }
    if (option_name == "--dup") {
      add_override("publish_dup", "--dup");
      continue;
    }
    if (option_name == "--payload") {
      add_override("publish_payload", "--payload");
      continue;
    }
    if (option_name == "--payload-stdin") {
      options.overrides.emplace_back("publish_payload_stdin", "true");
      continue;
    }
    if (option_name == "--payload-stdin-multiline") {
      options.overrides.emplace_back("publish_payload_stdin_multiline", "true");
      continue;
    }
    if (option_name == "--payload-file") {
      add_override("publish_payload_file", "--payload-file");
      continue;
    }
    if (option_name == "--payload-encoding") {
      add_override("publish_payload_encoding", "--payload-encoding");
      continue;
    }
    if (option_name == "--payload-format-indicator") {
      add_override("publish_payload_format_indicator",
                   "--payload-format-indicator");
      continue;
    }
    if (option_name == "--message-expiry-interval-seconds") {
      add_override("publish_message_expiry_interval_seconds",
                   "--message-expiry-interval-seconds");
      continue;
    }
    if (option_name == "--topic-alias") {
      add_override("publish_topic_alias", "--topic-alias");
      continue;
    }
    if (option_name == "--response-topic") {
      add_override("publish_response_topic", "--response-topic");
      continue;
    }
    if (option_name == "--correlation-data") {
      add_override("publish_correlation_data", "--correlation-data");
      continue;
    }
    if (option_name == "--correlation-data-encoding") {
      add_override("publish_correlation_data_encoding",
                   "--correlation-data-encoding");
      continue;
    }
    if (option_name == "--subscription-identifier") {
      add_override("publish_subscription_identifier",
                   "--subscription-identifier");
      continue;
    }
    if (option_name == "--content-type") {
      add_override("publish_content_type", "--content-type");
      continue;
    }
    if (option_name == "--publish-user-property") {
      add_override("publish_user_property", "--publish-user-property");
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

  if (command_name == "publish") {
    options.command = TestClientCommand::Publish;
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
      "  publish        Connect, publish one message, wait for QoS ACK flow, exit\n"
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
      "  --session-expiry-interval-seconds <seconds>\n"
      "  --receive-maximum <count>\n"
      "  --maximum-packet-size <bytes>\n"
      "  --topic-alias-maximum <count>\n"
      "  --request-response-information <true|false>\n"
      "  --request-problem-information <true|false>\n"
      "  --connect-user-property <name=value>   (repeatable)\n"
      "  --authentication-method <method>\n"
      "  --authentication-data <text>\n"
      "  --will-topic <topic>\n"
      "  --will-payload <text>\n"
      "  --will-qos <0|1|2>\n"
      "  --will-retain <true|false>\n"
      "  --will-delay-interval-seconds <seconds>\n"
      "  --will-payload-format-indicator <0|1>\n"
      "  --will-message-expiry-interval-seconds <seconds>\n"
      "  --will-content-type <text>\n"
      "  --will-response-topic <topic>\n"
      "  --will-correlation-data <text>\n"
      "  --will-user-property <name=value>      (repeatable)\n"
      "  --reconnect-period-ms <milliseconds>\n"
      "  --maximum-reconnect-times <count>\n"
      "  --topic <topic>\n"
      "  --qos <0|1|2>\n"
      "  --retain <true|false>\n"
      "  --dup <true|false>\n"
      "  --payload <text>\n"
      "  --payload-stdin\n"
      "  --payload-stdin-multiline\n"
      "  --payload-file <path>\n"
      "  --payload-encoding <raw|json|hex|base64|binary|protobuf|avro>\n"
      "  --payload-format-indicator <0|1>\n"
      "  --message-expiry-interval-seconds <seconds>\n"
      "  --topic-alias <value>\n"
      "  --response-topic <topic>\n"
      "  --correlation-data <text>\n"
      "  --correlation-data-encoding <raw|hex|base64>\n"
      "  --subscription-identifier <value>\n"
      "  --content-type <text>\n"
      "  --publish-user-property <name=value>   (repeatable)\n\n"
      "save-profile options:\n"
      "  --output <file>\n";
}

} // namespace mqtt
