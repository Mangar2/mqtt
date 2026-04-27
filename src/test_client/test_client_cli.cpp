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
    if (option_name == "--scenario") {
      options.scenario_name = require_value(index, argc, argv, "--scenario");
      ++index;
      continue;
    }
    if (option_name == "--list-scenarios") {
      options.list_scenarios = true;
      continue;
    }
    if (option_name == "--load-mode") {
      options.load_mode = require_value(index, argc, argv, "--load-mode");
      ++index;
      continue;
    }
    if (option_name == "--connection-count") {
      const std::string value =
          require_value(index, argc, argv, "--connection-count");
      options.load_connection_count = static_cast<uint32_t>(std::stoul(value));
      ++index;
      continue;
    }
    if (option_name == "--connect-interval-ms") {
      const std::string value =
          require_value(index, argc, argv, "--connect-interval-ms");
      options.load_connect_interval_ms =
          static_cast<uint32_t>(std::stoul(value));
      ++index;
      continue;
    }
    if (option_name == "--message-interval-ms") {
      const std::string value =
          require_value(index, argc, argv, "--message-interval-ms");
      options.load_message_interval_ms =
          static_cast<uint32_t>(std::stoul(value));
      ++index;
      continue;
    }
    if (option_name == "--publish-limit") {
      const std::string value =
          require_value(index, argc, argv, "--publish-limit");
      options.load_publish_limit = static_cast<uint32_t>(std::stoul(value));
      ++index;
      continue;
    }
    if (option_name == "--topic-template") {
      options.load_topic_template =
          require_value(index, argc, argv, "--topic-template");
      ++index;
      continue;
    }
    if (option_name == "--client-template") {
      options.load_client_template =
          require_value(index, argc, argv, "--client-template");
      ++index;
      continue;
    }
    if (option_name == "--metrics-json") {
      options.load_metrics_json = true;
      continue;
    }

    auto add_override = [&options, &index, argc,
                         argv](const std::string &key_name,
                               const std::string &flag_name) {
      const std::string value = require_value(index, argc, argv, flag_name);
      options.overrides.emplace_back(key_name, value);
      ++index;
    };

    // mqttx compatibility aliases (supported subset)
    if (option_name == "-t") {
      add_override("publish_topic", "-t");
      continue;
    }
    if (option_name == "-m" || option_name == "--message") {
      add_override("publish_payload", option_name);
      continue;
    }
    if (option_name == "-q") {
      add_override("publish_qos", "-q");
      continue;
    }
    if (option_name == "-r") {
      options.overrides.emplace_back("publish_retain", "true");
      continue;
    }
    if (option_name == "-d") {
      options.overrides.emplace_back("publish_dup", "true");
      continue;
    }
    if (option_name == "-s" || option_name == "--stdin") {
      options.overrides.emplace_back("publish_payload_stdin", "true");
      continue;
    }
    if (option_name == "-M" || option_name == "--multiline") {
      options.overrides.emplace_back("publish_payload_stdin_multiline", "true");
      continue;
    }
    if (option_name == "--file-read") {
      add_override("publish_payload_file", "--file-read");
      continue;
    }
    if (option_name == "-pf" || option_name == "--payload-format-indicator") {
      add_override("publish_payload_format_indicator", option_name);
      continue;
    }
    if (option_name == "-e") {
      add_override("publish_message_expiry_interval_seconds", "-e");
      continue;
    }
    if (option_name == "-ta") {
      add_override("publish_topic_alias", "-ta");
      continue;
    }
    if (option_name == "-rt") {
      add_override("publish_response_topic", "-rt");
      continue;
    }
    if (option_name == "-cd") {
      add_override("publish_correlation_data", "-cd");
      continue;
    }
    if (option_name == "-up" || option_name == "--user-properties") {
      add_override("publish_user_property", option_name);
      continue;
    }
    if (option_name == "-si") {
      add_override("publish_subscription_identifier", "-si");
      continue;
    }
    if (option_name == "-ct") {
      add_override("publish_content_type", "-ct");
      continue;
    }
    if (option_name == "-f" || option_name == "--format") {
      add_override("publish_payload_encoding", option_name);
      continue;
    }
    if (option_name == "-h" || option_name == "--hostname") {
      add_override("host", option_name);
      continue;
    }
    if (option_name == "-p") {
      add_override("port", "-p");
      continue;
    }
    if (option_name == "-i") {
      add_override("client_id", "-i");
      continue;
    }
    if (option_name == "--no-clean") {
      options.overrides.emplace_back("clean_start", "false");
      continue;
    }
    if (option_name == "-k" || option_name == "--keepalive") {
      add_override("keep_alive_seconds", option_name);
      continue;
    }
    if (option_name == "-u") {
      add_override("username", "-u");
      continue;
    }
    if (option_name == "-P") {
      add_override("password", "-P");
      continue;
    }
    if (option_name == "-l" || option_name == "--protocol") {
      add_override("transport", option_name);
      continue;
    }
    if (option_name == "--path") {
      add_override("ws_path", "--path");
      continue;
    }
    if (option_name == "-wh" || option_name == "--ws-headers") {
      add_override("ws_header", option_name);
      continue;
    }
    if (option_name == "-rp" || option_name == "--reconnect-period") {
      add_override("reconnect_period_ms", option_name);
      continue;
    }
    if (option_name == "-se") {
      add_override("session_expiry_interval_seconds", "-se");
      continue;
    }
    if (option_name == "--rcv-max") {
      add_override("receive_maximum", "--rcv-max");
      continue;
    }
    if (option_name == "--req-response-info") {
      options.overrides.emplace_back("request_response_information", "true");
      continue;
    }
    if (option_name == "--no-req-problem-info") {
      options.overrides.emplace_back("request_problem_information", "false");
      continue;
    }
    if (option_name == "-Cup" || option_name == "--conn-user-properties") {
      add_override("connect_user_property", option_name);
      continue;
    }
    if (option_name == "-Wt") {
      add_override("will_topic", "-Wt");
      continue;
    }
    if (option_name == "-Wm") {
      add_override("will_payload", "-Wm");
      continue;
    }
    if (option_name == "-Wq") {
      add_override("will_qos", "-Wq");
      continue;
    }
    if (option_name == "-Wr") {
      options.overrides.emplace_back("will_retain", "true");
      continue;
    }
    if (option_name == "-Wd") {
      add_override("will_delay_interval_seconds", "-Wd");
      continue;
    }
    if (option_name == "-Wpf") {
      add_override("will_payload_format_indicator", "-Wpf");
      continue;
    }
    if (option_name == "-We") {
      add_override("will_message_expiry_interval_seconds", "-We");
      continue;
    }
    if (option_name == "-Wct") {
      add_override("will_content_type", "-Wct");
      continue;
    }
    if (option_name == "-Wrt") {
      add_override("will_response_topic", "-Wrt");
      continue;
    }
    if (option_name == "-Wcd") {
      add_override("will_correlation_data", "-Wcd");
      continue;
    }
    if (option_name == "-Wup") {
      add_override("will_user_property", "-Wup");
      continue;
    }
    if (option_name == "-am") {
      add_override("authentication_method", "-am");
      continue;
    }
    if (option_name == "-V" || option_name == "--mqtt-version") {
      const std::string value = require_value(index, argc, argv, option_name);
      if (value != "5" && value != "5.0") {
        throw std::invalid_argument(
            "Only MQTT version 5.0 is supported by yahatestclient");
      }
      ++index;
      continue;
    }

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
    if (option_name == "--subscription") {
      add_override("subscribe_entry", "--subscription");
      continue;
    }
    if (option_name == "--subscribe-identifier") {
      add_override("subscribe_identifier", "--subscribe-identifier");
      continue;
    }
    if (option_name == "--subscribe-user-property") {
      add_override("subscribe_user_property", "--subscribe-user-property");
      continue;
    }
    if (option_name == "--clean-output") {
      options.overrides.emplace_back("subscribe_clean_output", "true");
      continue;
    }
    if (option_name == "--verbose-packets") {
      options.overrides.emplace_back("subscribe_verbose_packets", "true");
      continue;
    }
    if (option_name == "--output-file") {
      add_override("subscribe_output_file", "--output-file");
      continue;
    }
    if (option_name == "--append-output") {
      options.overrides.emplace_back("subscribe_output_append", "true");
      continue;
    }
    if (option_name == "--output-delimiter") {
      add_override("subscribe_output_delimiter", "--output-delimiter");
      continue;
    }
    if (option_name == "--output-format") {
      add_override("subscribe_output_format", "--output-format");
      continue;
    }
    if (option_name == "--message-limit") {
      add_override("subscribe_message_limit", "--message-limit");
      continue;
    }
    if (option_name == "--wait-timeout-ms") {
      add_override("subscribe_wait_timeout_ms", "--wait-timeout-ms");
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

  if (command_name == "publish" || command_name == "pub") {
    options.command = TestClientCommand::Publish;
    if (argc == 3 && std::string(argv[2]) == "--help") {
      options.command = TestClientCommand::Help;
      return options;
    }
    parse_common_options(options, argc, argv, 2);
    return options;
  }

  if (command_name == "subscribe") {
    options.command = TestClientCommand::Subscribe;
    parse_common_options(options, argc, argv, 2);
    return options;
  }

  if (command_name == "scenario") {
    options.command = TestClientCommand::Scenario;
    parse_common_options(options, argc, argv, 2);
    if (!options.list_scenarios && options.scenario_name.empty() &&
        options.load_mode.empty()) {
      throw std::invalid_argument(
          "scenario command requires --scenario <name>, --load-mode <name>, or --list-scenarios");
    }
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
      "  publish|pub    Connect, publish one message, wait for QoS ACK flow, exit\n"
      "  subscribe      Connect, subscribe, stream matching publishes, and optionally exit on message limit\n"
      "  scenario       Run built-in scripted scenario or list available scenarios\n"
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
      "  --publish-user-property <name=value>   (repeatable)\n"
      "  --subscription <filter|qos|no_local|retain_as_published|retain_handling> (repeatable)\n"
      "  --subscribe-identifier <value>\n"
      "  --subscribe-user-property <name=value> (repeatable)\n"
      "  --clean-output\n"
      "  --verbose-packets\n"
      "  --output-file <path>\n"
      "  --append-output\n"
      "  --output-delimiter <text>\n"
      "  --output-format <template>\n"
      "  --message-limit <count>\n"
      "  --wait-timeout-ms <milliseconds>\n\n"
      "mqttx compatibility aliases (supported subset):\n"
      "  command: pub\n"
      "  publish: -t -m --message -q -r -d -s --stdin -M --multiline\n"
      "           --file-read -pf -e -ta -rt -cd -up --user-properties\n"
      "           -si -ct -f --format\n"
      "  connection: -h --hostname -p -i --no-clean -k --keepalive -u -P\n"
      "              -l --protocol --path -wh --ws-headers -rp --reconnect-period\n"
      "              -se --rcv-max --req-response-info --no-req-problem-info\n"
      "              -Cup --conn-user-properties -am -V --mqtt-version\n"
      "  will: -Wt -Wm -Wq -Wr -Wd -Wpf -We -Wct -Wrt -Wcd -Wup\n\n"
      "scenario options:\n"
      "  --scenario <name>\n"
      "  --list-scenarios\n\n"
      "step32 load-mode options:\n"
      "  --load-mode <mass-connect|publish-rate|multi-subscribe>\n"
      "  --connection-count <count>\n"
      "  --connect-interval-ms <milliseconds>\n"
      "  --message-interval-ms <milliseconds>\n"
      "  --publish-limit <count>\n"
      "  --topic-template <template-with-{index}>\n"
      "  --client-template <template-with-{index}>\n"
      "  --metrics-json\n\n"
      "save-profile options:\n"
      "  --output <file>\n";
}

} // namespace mqtt
