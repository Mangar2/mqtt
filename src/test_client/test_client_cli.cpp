#include "test_client/test_client_cli.h"

#include <cctype>
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

[[nodiscard]] std::string normalize_mqttx_template(std::string value) {
  const std::string mqttx_index_token = "%i";
  const std::string internal_index_token = "{index}";

  std::size_t start = 0U;
  while (true) {
    const std::size_t found = value.find(mqttx_index_token, start);
    if (found == std::string::npos) {
      break;
    }
    value.replace(found, mqttx_index_token.size(), internal_index_token);
    start = found + internal_index_token.size();
  }
  return value;
}

[[nodiscard]] bool is_secure_protocol(std::string value) {
  for (char &character : value) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return value == "mqtts" || value == "wss";
}

[[nodiscard]] bool is_compact_qos_option(const std::string &option_name) {
  return option_name.size() == 3U && option_name[0] == '-' &&
         option_name[1] == 'q' &&
         (option_name[2] == '0' || option_name[2] == '1' || option_name[2] == '2');
}

[[nodiscard]] std::string compact_qos_value(const std::string &option_name) {
  return std::string(1U, option_name[2]);
}

[[nodiscard]] bool is_help_flag(const std::string &value) {
  return value == "--help" || value == "-h";
}

[[nodiscard]] bool parse_bool_literal_or_throw(const std::string &value,
                                               const std::string &option_name) {
  if (value == "true" || value == "1" || value == "yes") {
    return true;
  }
  if (value == "false" || value == "0" || value == "no") {
    return false;
  }
  throw std::invalid_argument("Invalid boolean value for option " + option_name);
}

void parse_bench_options(TestClientCliOptions &options,
                         const int argc,
                         const char *argv[],
                         int start_index) {
  const bool is_bench_conn = options.load_mode == "mass-connect";
  const bool is_bench_pub = options.load_mode == "publish-rate";
  const bool is_bench_sub = options.load_mode == "multi-subscribe";

  auto require_mode = [](const std::string &option_name, const bool allowed) {
    if (!allowed) {
      throw std::invalid_argument("Option " + option_name +
                                  " is not supported for this bench subcommand");
    }
  };

  for (int index = start_index; index < argc; ++index) {
    const std::string option_name = argv[index];

    auto add_override = [&options, &index, argc,
                         argv](const std::string &key_name,
                               const std::string &flag_name) {
      const std::string value = require_value(index, argc, argv, flag_name);
      options.overrides.emplace_back(key_name, value);
      ++index;
    };

    if (option_name == "-c" || option_name == "--count") {
      options.load_connection_count =
          static_cast<uint32_t>(std::stoul(require_value(index, argc, argv, option_name)));
      ++index;
      continue;
    }
    if (option_name == "-i" || option_name == "--interval") {
      options.load_connect_interval_ms =
          static_cast<uint32_t>(std::stoul(require_value(index, argc, argv, option_name)));
      ++index;
      continue;
    }
    if (option_name == "-im" || option_name == "--message-interval") {
      require_mode(option_name, is_bench_pub);
      options.load_message_interval_ms =
          static_cast<uint32_t>(std::stoul(require_value(index, argc, argv, option_name)));
      ++index;
      continue;
    }
    if (option_name == "-L" || option_name == "--limit") {
      require_mode(option_name, is_bench_pub || is_bench_sub);
      options.load_publish_limit =
          static_cast<uint32_t>(std::stoul(require_value(index, argc, argv, option_name)));
      ++index;
      continue;
    }
    if (option_name == "-t" || option_name == "--topic") {
      require_mode(option_name, is_bench_pub || is_bench_sub);
      options.load_topic_template =
          normalize_mqttx_template(require_value(index, argc, argv, option_name));
      ++index;
      if (is_bench_pub) {
        options.overrides.emplace_back("publish_topic", options.load_topic_template);
      } else {
        options.overrides.emplace_back("subscribe_entry",
                                       options.load_topic_template + "|0|false|false|0");
      }
      continue;
    }
    if (option_name == "-I" || option_name == "--client-id") {
      const std::string normalized =
          normalize_mqttx_template(require_value(index, argc, argv, option_name));
      options.load_client_template = normalized;
      options.overrides.emplace_back("client_id", normalized);
      ++index;
      continue;
    }
    if (option_name == "-m" || option_name == "--message") {
      require_mode(option_name, is_bench_pub);
      add_override("publish_payload", option_name);
      continue;
    }
    if (option_name == "-q") {
      require_mode(option_name, is_bench_pub || is_bench_sub);
      if (is_bench_pub) {
        add_override("publish_qos", option_name);
      } else {
        options.load_subscribe_qos = static_cast<uint8_t>(
            std::stoul(require_value(index, argc, argv, option_name)));
        ++index;
      }
      continue;
    }
    if (is_compact_qos_option(option_name)) {
      require_mode(option_name, is_bench_pub || is_bench_sub);
      if (is_bench_pub) {
        options.overrides.emplace_back("publish_qos", compact_qos_value(option_name));
      } else {
        options.load_subscribe_qos = static_cast<uint8_t>(
            std::stoul(compact_qos_value(option_name)));
      }
      continue;
    }
    if (option_name == "-r") {
      require_mode(option_name, is_bench_pub);
      options.overrides.emplace_back("publish_retain", "true");
      continue;
    }
    if (option_name == "-d") {
      require_mode(option_name, is_bench_pub);
      options.overrides.emplace_back("publish_dup", "true");
      continue;
    }
    if (option_name == "-pf" || option_name == "--payload-format-indicator") {
      require_mode(option_name, is_bench_pub);
      add_override("publish_payload_format_indicator", option_name);
      continue;
    }
    if (option_name == "-e" || option_name == "--message-expiry-interval") {
      require_mode(option_name, is_bench_pub);
      add_override("publish_message_expiry_interval_seconds", option_name);
      continue;
    }
    if (option_name == "-ta" || option_name == "--topic-alias") {
      require_mode(option_name, is_bench_pub);
      add_override("publish_topic_alias", option_name);
      continue;
    }
    if (option_name == "-rt" || option_name == "--response-topic") {
      require_mode(option_name, is_bench_pub);
      add_override("publish_response_topic", option_name);
      continue;
    }
    if (option_name == "-cd" || option_name == "--correlation-data") {
      require_mode(option_name, is_bench_pub);
      add_override("publish_correlation_data", option_name);
      continue;
    }
    if (option_name == "-up" || option_name == "--user-properties") {
      if (is_bench_conn) {
        add_override("connect_user_property", option_name);
      } else if (is_bench_pub) {
        add_override("publish_user_property", option_name);
      } else {
        add_override("subscribe_user_property", option_name);
      }
      continue;
    }
    if (option_name == "-si" || option_name == "--subscription-identifier") {
      require_mode(option_name, is_bench_pub || is_bench_sub);
      if (is_bench_pub) {
        add_override("publish_subscription_identifier", option_name);
      } else {
        options.load_subscribe_identifier_set = true;
        options.load_subscribe_identifier =
            static_cast<uint32_t>(std::stoul(require_value(index, argc, argv, option_name)));
        ++index;
      }
      continue;
    }
    if (option_name == "-ct" || option_name == "--content-type") {
      require_mode(option_name, is_bench_pub);
      add_override("publish_content_type", option_name);
      continue;
    }
    if (option_name == "-v" || option_name == "--verbose") {
      options.load_verbose = true;
      continue;
    }
    if (option_name == "--metrics-json") {
      options.load_metrics_json = true;
      continue;
    }
    if (option_name == "-h" || option_name == "--hostname") {
      add_override("host", option_name);
      continue;
    }
    if (option_name == "-p" || option_name == "--port") {
      add_override("port", option_name);
      continue;
    }
    if (option_name == "--no-clean") {
      options.overrides.emplace_back("clean_start", "false");
      continue;
    }
    if (option_name == "-k" || option_name == "--keepalive") {
      require_mode(option_name, is_bench_conn || is_bench_pub);
      add_override("keep_alive_seconds", option_name);
      continue;
    }
    if (option_name == "-u" || option_name == "--username") {
      add_override("username", option_name);
      continue;
    }
    if (option_name == "-P" || option_name == "--password") {
      add_override("password", option_name);
      continue;
    }
    if (option_name == "-l" || option_name == "--protocol") {
      const std::string protocol = require_value(index, argc, argv, option_name);
      if (is_secure_protocol(protocol)) {
        throw std::invalid_argument(
            "Secure transports mqtts/wss are intentionally unsupported");
      }
      options.overrides.emplace_back("transport", protocol);
      ++index;
      continue;
    }
    if (option_name == "--path") {
      add_override("ws_path", option_name);
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
    if (option_name == "--maximum-reconnect-times") {
      add_override("maximum_reconnect_times", option_name);
      continue;
    }
    if (option_name == "--maximun-reconnect-times") {
      add_override("maximum_reconnect_times", option_name);
      continue;
    }
    if (option_name == "-se" || option_name == "--session-expiry-interval") {
      add_override("session_expiry_interval_seconds", option_name);
      continue;
    }
    if (option_name == "--rcv-max" || option_name == "--receive-maximum") {
      add_override("receive_maximum", option_name);
      continue;
    }
    if (option_name == "--maximum-packet-size") {
      add_override("maximum_packet_size", option_name);
      continue;
    }
    if (option_name == "--topic-alias-maximum") {
      add_override("topic_alias_maximum", option_name);
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
      require_mode(option_name, is_bench_pub || is_bench_sub);
      add_override("connect_user_property", option_name);
      continue;
    }
    if (option_name == "-Wt" || option_name == "--will-topic") {
      add_override("will_topic", option_name);
      continue;
    }
    if (option_name == "-Wm" || option_name == "--will-message") {
      add_override("will_payload", option_name);
      continue;
    }
    if (option_name == "-Wq" || option_name == "--will-qos") {
      add_override("will_qos", option_name);
      continue;
    }
    if (option_name == "-Wr" || option_name == "--will-retain") {
      options.overrides.emplace_back("will_retain", "true");
      continue;
    }
    if (option_name == "-Wd" || option_name == "--will-delay-interval") {
      add_override("will_delay_interval_seconds", option_name);
      continue;
    }
    if (option_name == "-Wpf" || option_name == "--will-payload-format-indicator") {
      add_override("will_payload_format_indicator", option_name);
      continue;
    }
    if (option_name == "-We" || option_name == "--will-message-expiry-interval") {
      add_override("will_message_expiry_interval_seconds", option_name);
      continue;
    }
    if (option_name == "-Wct" || option_name == "--will-content-type") {
      add_override("will_content_type", option_name);
      continue;
    }
    if (option_name == "-Wrt" || option_name == "--will-response-topic") {
      add_override("will_response_topic", option_name);
      continue;
    }
    if (option_name == "-Wcd" || option_name == "--will-correlation-data") {
      add_override("will_correlation_data", option_name);
      continue;
    }
    if (option_name == "-Wup" || option_name == "--will-user-properties") {
      add_override("will_user_property", option_name);
      continue;
    }
    if (option_name == "--file-read") {
      require_mode(option_name, is_bench_pub);
      add_override("publish_payload_file", option_name);
      continue;
    }
    if (option_name == "-f" || option_name == "--format") {
      require_mode(option_name, is_bench_pub);
      add_override("publish_payload_encoding", option_name);
      continue;
    }
    if (option_name == "-Pp" || option_name == "--protobuf-path") {
      require_mode(option_name, is_bench_pub);
      add_override("publish_protobuf_path", option_name);
      continue;
    }
    if (option_name == "-Pmn" || option_name == "--protobuf-message-name") {
      require_mode(option_name, is_bench_pub);
      add_override("publish_protobuf_message_name", option_name);
      continue;
    }
    if (option_name == "-Ap" || option_name == "--avsc-path") {
      require_mode(option_name, is_bench_pub);
      add_override("publish_avsc_path", option_name);
      continue;
    }
    if (option_name == "--split") {
      require_mode(option_name, is_bench_pub);
      options.load_split_enabled = true;
      options.load_split_delimiter = ",";
      if (has_more_arguments(index, argc) && argv[index + 1][0] != '-') {
        options.load_split_delimiter = argv[index + 1];
        ++index;
      }
      continue;
    }
    if (option_name == "-S" || option_name == "--payload-size") {
      require_mode(option_name, is_bench_pub);
      options.load_payload_size =
          static_cast<uint32_t>(std::stoul(require_value(index, argc, argv, option_name)));
      ++index;
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

    if (option_name == "-so" || option_name == "--save-options") {
      throw std::invalid_argument(
          "Option " + option_name +
          " is recognized but not implemented in mqttx-compatible paths");
    }
    if (option_name == "-lo" || option_name == "--load-options") {
      throw std::invalid_argument(
          "Option " + option_name +
          " is recognized but not implemented in mqttx-compatible paths");
    }
    if (option_name == "--debug") {
      throw std::invalid_argument(
          "Option --debug is recognized but not implemented in mqttx-compatible paths");
    }
    if (option_name == "-nl" || option_name == "--no_local" ||
        option_name == "-rap" || option_name == "--retain-as-published" ||
        option_name == "-rh" || option_name == "--retain-handling") {
      require_mode(option_name, is_bench_sub);
      if (option_name == "-nl" || option_name == "--no_local") {
        bool value = true;
        if (has_more_arguments(index, argc) && argv[index + 1][0] != '-') {
          value = parse_bool_literal_or_throw(argv[index + 1], option_name);
          ++index;
        }
        options.load_subscribe_no_local = value;
      } else if (option_name == "-rap" ||
                 option_name == "--retain-as-published") {
        bool value = true;
        if (has_more_arguments(index, argc) && argv[index + 1][0] != '-') {
          value = parse_bool_literal_or_throw(argv[index + 1], option_name);
          ++index;
        }
        options.load_subscribe_retain_as_published = value;
      } else {
        options.load_subscribe_retain_handling = static_cast<uint8_t>(
            std::stoul(require_value(index, argc, argv, option_name)));
        ++index;
      }
      continue;
    }

    if (option_name == "--key" || option_name == "--cert" ||
        option_name == "--ca" || option_name == "--insecure" ||
        option_name == "--alpn") {
      throw std::invalid_argument(
          "Secure TLS options are intentionally unsupported");
    }

    throw std::invalid_argument("Unknown option: " + option_name);
  }
}

void parse_mqttx_sub_options(TestClientCliOptions &options,
                             const int argc,
                             const char *argv[],
                             int start_index) {
  std::vector<std::string> topic_filters;
  uint8_t subscribe_qos = 0U;
  bool subscribe_no_local = false;
  bool subscribe_retain_as_published = false;
  uint8_t subscribe_retain_handling = 0U;

  auto add_override = [&options, argc,
                       argv](int &index, const std::string &key_name,
                             const std::string &flag_name) {
    const std::string value = require_value(index, argc, argv, flag_name);
    options.overrides.emplace_back(key_name, value);
    ++index;
  };

  for (int index = start_index; index < argc; ++index) {
    const std::string option_name = argv[index];

    if (option_name == "-t" || option_name == "--topic") {
      topic_filters.push_back(require_value(index, argc, argv, option_name));
      ++index;
      continue;
    }
    if (option_name == "-q" || option_name == "--qos") {
      subscribe_qos = static_cast<uint8_t>(
          std::stoul(require_value(index, argc, argv, option_name)));
      ++index;
      continue;
    }
    if (is_compact_qos_option(option_name)) {
      subscribe_qos = static_cast<uint8_t>(std::stoul(compact_qos_value(option_name)));
      continue;
    }
    if (option_name == "-nl" || option_name == "--no_local") {
      bool value = true;
      if (has_more_arguments(index, argc) && argv[index + 1][0] != '-') {
        value = parse_bool_literal_or_throw(argv[index + 1], option_name);
        ++index;
      }
      subscribe_no_local = value;
      continue;
    }
    if (option_name == "-rap" || option_name == "--retain-as-published") {
      bool value = true;
      if (has_more_arguments(index, argc) && argv[index + 1][0] != '-') {
        value = parse_bool_literal_or_throw(argv[index + 1], option_name);
        ++index;
      }
      subscribe_retain_as_published = value;
      continue;
    }
    if (option_name == "-rh" || option_name == "--retain-handling") {
      subscribe_retain_handling = static_cast<uint8_t>(
          std::stoul(require_value(index, argc, argv, option_name)));
      ++index;
      continue;
    }
    if (option_name == "-si" || option_name == "--subscription-identifier") {
      add_override(index, "subscribe_identifier", option_name);
      continue;
    }
    if (option_name == "-up" || option_name == "--user-properties") {
      add_override(index, "subscribe_user_property", option_name);
      continue;
    }
    if (option_name == "-v" || option_name == "--verbose") {
      options.overrides.emplace_back("subscribe_verbose_packets", "true");
      continue;
    }
    if (option_name == "--output-mode") {
      const std::string mode = require_value(index, argc, argv, option_name);
      if (mode == "clean") {
        options.overrides.emplace_back("subscribe_clean_output", "true");
      } else if (mode == "default") {
        options.overrides.emplace_back("subscribe_clean_output", "false");
      } else {
        throw std::invalid_argument("Unsupported --output-mode: " + mode);
      }
      ++index;
      continue;
    }
    if (option_name == "--file-write") {
      add_override(index, "subscribe_output_file", option_name);
      options.overrides.emplace_back("subscribe_output_append", "true");
      continue;
    }
    if (option_name == "--file-save") {
      add_override(index, "subscribe_output_file_save", option_name);
      continue;
    }
    if (option_name == "--delimiter") {
      std::string delimiter = "\n";
      if (has_more_arguments(index, argc) && argv[index + 1][0] != '-') {
        delimiter = argv[index + 1];
        ++index;
      }
      options.overrides.emplace_back("subscribe_output_delimiter", delimiter);
      continue;
    }
    if (option_name == "-f" || option_name == "--format") {
      add_override(index, "subscribe_payload_format", option_name);
      continue;
    }
    if (option_name == "-Pp" || option_name == "--protobuf-path") {
      add_override(index, "subscribe_protobuf_path", option_name);
      continue;
    }
    if (option_name == "-Pmn" || option_name == "--protobuf-message-name") {
      add_override(index, "subscribe_protobuf_message_name", option_name);
      continue;
    }
    if (option_name == "-Ap" || option_name == "--avsc-path") {
      add_override(index, "subscribe_avsc_path", option_name);
      continue;
    }
    if (option_name == "--message-limit") {
      add_override(index, "subscribe_message_limit", option_name);
      continue;
    }
    if (option_name == "--wait-timeout-ms") {
      add_override(index, "subscribe_wait_timeout_ms", option_name);
      continue;
    }
    if (option_name == "-h" || option_name == "--hostname") {
      add_override(index, "host", option_name);
      continue;
    }
    if (option_name == "-p" || option_name == "--port") {
      add_override(index, "port", option_name);
      continue;
    }
    if (option_name == "-i" || option_name == "--client-id") {
      add_override(index, "client_id", option_name);
      continue;
    }
    if (option_name == "--no-clean") {
      options.overrides.emplace_back("clean_start", "false");
      continue;
    }
    if (option_name == "-k" || option_name == "--keepalive") {
      add_override(index, "keep_alive_seconds", option_name);
      continue;
    }
    if (option_name == "-u" || option_name == "--username") {
      add_override(index, "username", option_name);
      continue;
    }
    if (option_name == "-P" || option_name == "--password") {
      add_override(index, "password", option_name);
      continue;
    }
    if (option_name == "-l" || option_name == "--protocol") {
      const std::string protocol = require_value(index, argc, argv, option_name);
      if (is_secure_protocol(protocol)) {
        throw std::invalid_argument(
            "Secure transports mqtts/wss are intentionally unsupported");
      }
      options.overrides.emplace_back("transport", protocol);
      ++index;
      continue;
    }
    if (option_name == "--path") {
      add_override(index, "ws_path", option_name);
      continue;
    }
    if (option_name == "-wh" || option_name == "--ws-headers") {
      add_override(index, "ws_header", option_name);
      continue;
    }
    if (option_name == "-rp" || option_name == "--reconnect-period") {
      add_override(index, "reconnect_period_ms", option_name);
      continue;
    }
    if (option_name == "--maximum-reconnect-times") {
      add_override(index, "maximum_reconnect_times", option_name);
      continue;
    }
    if (option_name == "--maximun-reconnect-times") {
      add_override(index, "maximum_reconnect_times", option_name);
      continue;
    }
    if (option_name == "--debug" || option_name == "-so" ||
        option_name == "--save-options" || option_name == "-lo" ||
        option_name == "--load-options") {
      throw std::invalid_argument(
          "Option " + option_name +
          " is recognized but not implemented in mqttx-compatible paths");
    }
    if (option_name == "--key" || option_name == "--cert" ||
        option_name == "--ca" || option_name == "--insecure" ||
        option_name == "--alpn") {
      throw std::invalid_argument(
          "Secure TLS options are intentionally unsupported");
    }

    throw std::invalid_argument("Unknown option: " + option_name);
  }

  if (topic_filters.empty()) {
    throw std::invalid_argument("sub command requires -t/--topic");
  }
  for (const std::string &filter : topic_filters) {
    options.overrides.emplace_back(
        "subscribe_entry",
        filter + "|" + std::to_string(subscribe_qos) + "|" +
            (subscribe_no_local ? "true" : "false") + "|" +
            (subscribe_retain_as_published ? "true" : "false") + "|" +
            std::to_string(subscribe_retain_handling));
  }
}

void parse_mqttx_simulate_options(TestClientCliOptions &options,
                                  const int argc,
                                  const char *argv[],
                                  int start_index) {
  options.command = TestClientCommand::Scenario;

  bool has_selector = false;

  for (int index = start_index; index < argc; ++index) {
    const std::string option_name = argv[index];

    auto add_override = [&options, argc,
                         argv](int &argument_index,
                               const std::string &key_name,
                               const std::string &flag_name) {
      const std::string value =
          require_value(argument_index, argc, argv, flag_name);
      options.overrides.emplace_back(key_name, value);
      ++argument_index;
    };

    if (option_name == "-sc" || option_name == "--scenario") {
      options.scenario_name = require_value(index, argc, argv, option_name);
      has_selector = true;
      ++index;
      continue;
    }
    if (option_name == "-f" || option_name == "--file") {
      options.output_path = require_value(index, argc, argv, option_name);
      has_selector = true;
      ++index;
      continue;
    }
    if (option_name == "-c" || option_name == "--count") {
      options.load_connection_count = static_cast<uint32_t>(
          std::stoul(require_value(index, argc, argv, option_name)));
      ++index;
      continue;
    }
    if (option_name == "-i" || option_name == "--interval") {
      options.load_connect_interval_ms = static_cast<uint32_t>(
          std::stoul(require_value(index, argc, argv, option_name)));
      ++index;
      continue;
    }
    if (option_name == "-im" || option_name == "--message-interval") {
      options.load_message_interval_ms = static_cast<uint32_t>(
          std::stoul(require_value(index, argc, argv, option_name)));
      ++index;
      continue;
    }
    if (option_name == "-L" || option_name == "--limit") {
      options.load_publish_limit = static_cast<uint32_t>(
          std::stoul(require_value(index, argc, argv, option_name)));
      ++index;
      continue;
    }
    if (option_name == "-t" || option_name == "--topic") {
      options.load_topic_template =
          normalize_mqttx_template(require_value(index, argc, argv, option_name));
      ++index;
      continue;
    }
    if (option_name == "-I" || option_name == "--client-id") {
      options.load_client_template =
          normalize_mqttx_template(require_value(index, argc, argv, option_name));
      ++index;
      continue;
    }
    if (option_name == "-m" || option_name == "--message") {
      add_override(index, "publish_payload", option_name);
      continue;
    }
    if (option_name == "-q" || option_name == "--qos") {
      add_override(index, "publish_qos", option_name);
      continue;
    }
    if (is_compact_qos_option(option_name)) {
      options.overrides.emplace_back("publish_qos", compact_qos_value(option_name));
      continue;
    }
    if (option_name == "-r" || option_name == "--retain") {
      options.overrides.emplace_back("publish_retain", "true");
      continue;
    }
    if (option_name == "-v" || option_name == "--verbose") {
      options.load_verbose = true;
      continue;
    }
    if (option_name == "--metrics-json") {
      options.load_metrics_json = true;
      continue;
    }
    if (option_name == "-h" || option_name == "--hostname") {
      add_override(index, "host", option_name);
      continue;
    }
    if (option_name == "-p" || option_name == "--port") {
      add_override(index, "port", option_name);
      continue;
    }
    if (option_name == "--no-clean") {
      options.overrides.emplace_back("clean_start", "false");
      continue;
    }
    if (option_name == "-k" || option_name == "--keepalive") {
      add_override(index, "keep_alive_seconds", option_name);
      continue;
    }
    if (option_name == "-u" || option_name == "--username") {
      add_override(index, "username", option_name);
      continue;
    }
    if (option_name == "-P" || option_name == "--password") {
      add_override(index, "password", option_name);
      continue;
    }
    if (option_name == "-l" || option_name == "--protocol") {
      const std::string protocol = require_value(index, argc, argv, option_name);
      if (is_secure_protocol(protocol)) {
        throw std::invalid_argument(
            "Secure transports mqtts/wss are intentionally unsupported");
      }
      options.overrides.emplace_back("transport", protocol);
      ++index;
      continue;
    }
    if (option_name == "--path") {
      add_override(index, "ws_path", option_name);
      continue;
    }
    if (option_name == "-wh" || option_name == "--ws-headers") {
      add_override(index, "ws_header", option_name);
      continue;
    }
    if (option_name == "-rp" || option_name == "--reconnect-period") {
      add_override(index, "reconnect_period_ms", option_name);
      continue;
    }
    if (option_name == "--maximum-reconnect-times" ||
        option_name == "--maximun-reconnect-times") {
      add_override(index, "maximum_reconnect_times", option_name);
      continue;
    }
    if (option_name == "-se" || option_name == "--session-expiry-interval") {
      add_override(index, "session_expiry_interval_seconds", option_name);
      continue;
    }
    if (option_name == "--rcv-max" || option_name == "--receive-maximum") {
      add_override(index, "receive_maximum", option_name);
      continue;
    }
    if (option_name == "--maximum-packet-size") {
      add_override(index, "maximum_packet_size", option_name);
      continue;
    }
    if (option_name == "--topic-alias-maximum") {
      add_override(index, "topic_alias_maximum", option_name);
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
      add_override(index, "connect_user_property", option_name);
      continue;
    }
    if (option_name == "-Wt" || option_name == "--will-topic") {
      add_override(index, "will_topic", option_name);
      continue;
    }
    if (option_name == "-Wm" || option_name == "--will-message") {
      add_override(index, "will_payload", option_name);
      continue;
    }
    if (option_name == "-Wq" || option_name == "--will-qos") {
      add_override(index, "will_qos", option_name);
      continue;
    }
    if (option_name == "-Wr" || option_name == "--will-retain") {
      options.overrides.emplace_back("will_retain", "true");
      continue;
    }
    if (option_name == "-Wd" || option_name == "--will-delay-interval") {
      add_override(index, "will_delay_interval_seconds", option_name);
      continue;
    }
    if (option_name == "-Wpf" || option_name == "--will-payload-format-indicator") {
      add_override(index, "will_payload_format_indicator", option_name);
      continue;
    }
    if (option_name == "-We" || option_name == "--will-message-expiry-interval") {
      add_override(index, "will_message_expiry_interval_seconds", option_name);
      continue;
    }
    if (option_name == "-Wct" || option_name == "--will-content-type") {
      add_override(index, "will_content_type", option_name);
      continue;
    }
    if (option_name == "-Wrt" || option_name == "--will-response-topic") {
      add_override(index, "will_response_topic", option_name);
      continue;
    }
    if (option_name == "-Wcd" || option_name == "--will-correlation-data") {
      add_override(index, "will_correlation_data", option_name);
      continue;
    }
    if (option_name == "-Wup" || option_name == "--will-user-properties") {
      add_override(index, "will_user_property", option_name);
      continue;
    }
    if (option_name == "-f" || option_name == "--format") {
      add_override(index, "publish_payload_encoding", option_name);
      continue;
    }
    if (option_name == "-Pp" || option_name == "--protobuf-path") {
      add_override(index, "publish_protobuf_path", option_name);
      continue;
    }
    if (option_name == "-Pmn" || option_name == "--protobuf-message-name") {
      add_override(index, "publish_protobuf_message_name", option_name);
      continue;
    }
    if (option_name == "-Ap" || option_name == "--avsc-path") {
      add_override(index, "publish_avsc_path", option_name);
      continue;
    }
    if (option_name == "-S" || option_name == "--payload-size") {
      options.load_payload_size = static_cast<uint32_t>(
          std::stoul(require_value(index, argc, argv, option_name)));
      ++index;
      continue;
    }
    if (option_name == "-am" || option_name == "--authentication-method") {
      add_override(index, "authentication_method", option_name);
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

    if (option_name == "-so" || option_name == "--save-options" ||
        option_name == "-lo" || option_name == "--load-options" ||
        option_name == "--debug") {
      throw std::invalid_argument(
          "Option " + option_name +
          " is recognized but not implemented in mqttx-compatible paths");
    }

    if (option_name == "--key" || option_name == "--cert" ||
        option_name == "--ca" || option_name == "--insecure" ||
        option_name == "--alpn") {
      throw std::invalid_argument(
          "Secure TLS options are intentionally unsupported");
    }

    throw std::invalid_argument("Unknown option: " + option_name);
  }

  if (options.scenario_name == "mass-connect" ||
      options.scenario_name == "publish-rate" ||
      options.scenario_name == "multi-subscribe") {
    options.load_mode = options.scenario_name;
    options.scenario_name.clear();
    return;
  }

  if (!options.scenario_name.empty()) {
    return;
  }

  if (!options.output_path.empty()) {
    options.scenario_name = "script-file:" + options.output_path;
    return;
  }

  if (!has_selector) {
    throw std::invalid_argument(
        "simulate command requires -sc/--scenario or -f/--file");
  }
}

void parse_mqttx_pub_options(TestClientCliOptions &options,
                             const int argc,
                             const char *argv[],
                             int start_index) {
  for (int index = start_index; index < argc; ++index) {
    const std::string option_name = argv[index];

    auto add_override = [&options, &index, argc,
                         argv](const std::string &key_name,
                               const std::string &flag_name) {
      const std::string value = require_value(index, argc, argv, flag_name);
      options.overrides.emplace_back(key_name, value);
      ++index;
    };

    if (option_name == "-t" || option_name == "--topic") {
      add_override("publish_topic", option_name);
      continue;
    }
    if (option_name == "-m" || option_name == "--message") {
      add_override("publish_payload", option_name);
      continue;
    }
    if (option_name == "-q") {
      add_override("publish_qos", option_name);
      continue;
    }
    if (is_compact_qos_option(option_name)) {
      options.overrides.emplace_back("publish_qos", compact_qos_value(option_name));
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
    if (option_name == "-lm" || option_name == "--line-mode") {
      options.overrides.emplace_back("publish_payload_stdin", "true");
      options.overrides.emplace_back("publish_payload_stdin_multiline", "true");
      continue;
    }
    if (option_name == "-pf" || option_name == "--payload-format-indicator") {
      add_override("publish_payload_format_indicator", option_name);
      continue;
    }
    if (option_name == "-e" || option_name == "--message-expiry-interval") {
      add_override("publish_message_expiry_interval_seconds", option_name);
      continue;
    }
    if (option_name == "-ta" || option_name == "--topic-alias") {
      add_override("publish_topic_alias", option_name);
      continue;
    }
    if (option_name == "-rt" || option_name == "--response-topic") {
      add_override("publish_response_topic", option_name);
      continue;
    }
    if (option_name == "-cd" || option_name == "--correlation-data") {
      add_override("publish_correlation_data", option_name);
      continue;
    }
    if (option_name == "-up" || option_name == "--user-properties") {
      add_override("publish_user_property", option_name);
      continue;
    }
    if (option_name == "-si" || option_name == "--subscription-identifier") {
      add_override("publish_subscription_identifier", option_name);
      continue;
    }
    if (option_name == "-ct" || option_name == "--content-type") {
      add_override("publish_content_type", option_name);
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
    if (option_name == "-h" || option_name == "--hostname") {
      add_override("host", option_name);
      continue;
    }
    if (option_name == "-p" || option_name == "--port") {
      add_override("port", option_name);
      continue;
    }
    if (option_name == "-f" || option_name == "--format") {
      add_override("publish_payload_encoding", option_name);
      continue;
    }
    if (option_name == "-i" || option_name == "--client-id") {
      add_override("client_id", option_name);
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
    if (option_name == "-u" || option_name == "--username") {
      add_override("username", option_name);
      continue;
    }
    if (option_name == "-P" || option_name == "--password") {
      add_override("password", option_name);
      continue;
    }
    if (option_name == "-l" || option_name == "--protocol") {
      const std::string protocol = require_value(index, argc, argv, option_name);
      if (is_secure_protocol(protocol)) {
        throw std::invalid_argument(
            "Secure transports mqtts/wss are intentionally unsupported");
      }
      options.overrides.emplace_back("transport", protocol);
      ++index;
      continue;
    }
    if (option_name == "--path") {
      add_override("ws_path", option_name);
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
    if (option_name == "--maximum-reconnect-times") {
      add_override("maximum_reconnect_times", option_name);
      continue;
    }
    if (option_name == "--maximun-reconnect-times") {
      add_override("maximum_reconnect_times", option_name);
      continue;
    }
    if (option_name == "-se" || option_name == "--session-expiry-interval") {
      add_override("session_expiry_interval_seconds", option_name);
      continue;
    }
    if (option_name == "--rcv-max" || option_name == "--receive-maximum") {
      add_override("receive_maximum", option_name);
      continue;
    }
    if (option_name == "--maximum-packet-size") {
      add_override("maximum_packet_size", option_name);
      continue;
    }
    if (option_name == "--topic-alias-maximum") {
      add_override("topic_alias_maximum", option_name);
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
    if (option_name == "-Wt" || option_name == "--will-topic") {
      add_override("will_topic", option_name);
      continue;
    }
    if (option_name == "-Wm" || option_name == "--will-message") {
      add_override("will_payload", option_name);
      continue;
    }
    if (option_name == "-Wq" || option_name == "--will-qos") {
      add_override("will_qos", option_name);
      continue;
    }
    if (option_name == "-Wr" || option_name == "--will-retain") {
      options.overrides.emplace_back("will_retain", "true");
      continue;
    }
    if (option_name == "-Wd" || option_name == "--will-delay-interval") {
      add_override("will_delay_interval_seconds", option_name);
      continue;
    }
    if (option_name == "-Wpf" || option_name == "--will-payload-format-indicator") {
      add_override("will_payload_format_indicator", option_name);
      continue;
    }
    if (option_name == "-We" || option_name == "--will-message-expiry-interval") {
      add_override("will_message_expiry_interval_seconds", option_name);
      continue;
    }
    if (option_name == "-Wct" || option_name == "--will-content-type") {
      add_override("will_content_type", option_name);
      continue;
    }
    if (option_name == "-Wrt" || option_name == "--will-response-topic") {
      add_override("will_response_topic", option_name);
      continue;
    }
    if (option_name == "-Wcd" || option_name == "--will-correlation-data") {
      add_override("will_correlation_data", option_name);
      continue;
    }
    if (option_name == "-Wup" || option_name == "--will-user-properties") {
      add_override("will_user_property", option_name);
      continue;
    }
    if (option_name == "-so" || option_name == "--save-options") {
      throw std::invalid_argument(
          "Option " + option_name +
          " is recognized but not implemented in mqttx-compatible paths");
    }
    if (option_name == "-lo" || option_name == "--load-options") {
      throw std::invalid_argument(
          "Option " + option_name +
          " is recognized but not implemented in mqttx-compatible paths");
    }
    if (option_name == "--file-read") {
      add_override("publish_payload_file", option_name);
      continue;
    }
    if (option_name == "-Pp" || option_name == "--protobuf-path") {
      add_override("publish_protobuf_path", option_name);
      continue;
    }
    if (option_name == "-Pmn" || option_name == "--protobuf-message-name") {
      add_override("publish_protobuf_message_name", option_name);
      continue;
    }
    if (option_name == "-Ap" || option_name == "--avsc-path") {
      add_override("publish_avsc_path", option_name);
      continue;
    }
    if (option_name == "-S" || option_name == "--payload-size") {
      add_override("publish_payload_size", option_name);
      continue;
    }
    if (option_name == "--debug") {
      throw std::invalid_argument(
          "Option --debug is recognized but not implemented in mqttx-compatible paths");
    }
    if (option_name == "--key" || option_name == "--cert" ||
        option_name == "--ca" || option_name == "--insecure" ||
        option_name == "--alpn") {
      throw std::invalid_argument(
          "Secure TLS options are intentionally unsupported");
    }
    if (option_name == "-am" || option_name == "--authentication-method") {
      add_override("authentication_method", option_name);
      continue;
    }

    throw std::invalid_argument("Unknown option: " + option_name);
  }
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
    if (option_name == "--parallelism") {
      const std::string value =
          require_value(index, argc, argv, "--parallelism");
      options.load_parallelism = static_cast<uint32_t>(std::stoul(value));
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
    if (option_name == "--maximun-reconnect-times") {
      add_override("maximum_reconnect_times", "--maximun-reconnect-times");
      continue;
    }
    if (option_name == "-so" || option_name == "--save-options") {
      throw std::invalid_argument(
          "Option " + option_name +
          " is recognized but not implemented in mqttx-compatible paths");
    }
    if (option_name == "-lo" || option_name == "--load-options") {
      throw std::invalid_argument(
          "Option " + option_name +
          " is recognized but not implemented in mqttx-compatible paths");
    }
    if (option_name == "--debug") {
      throw std::invalid_argument(
          "Option --debug is recognized but not implemented in mqttx-compatible paths");
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
    if (option_name == "--subscribe-payload-format") {
      add_override("subscribe_payload_format", "--subscribe-payload-format");
      continue;
    }
    if (option_name == "--subscribe-protobuf-path") {
      add_override("subscribe_protobuf_path", "--subscribe-protobuf-path");
      continue;
    }
    if (option_name == "--subscribe-protobuf-message-name") {
      add_override("subscribe_protobuf_message_name",
                   "--subscribe-protobuf-message-name");
      continue;
    }
    if (option_name == "--subscribe-avsc-path") {
      add_override("subscribe_avsc_path", "--subscribe-avsc-path");
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
    if (option_name == "--output-file-save") {
      add_override("subscribe_output_file_save", "--output-file-save");
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
  if (command_name == "--version" || command_name == "-v") {
    options.command = TestClientCommand::Version;
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

  if (command_name == "pub") {
    options.command = TestClientCommand::Publish;
    if (argc == 3 &&
        (std::string(argv[2]) == "--help" || std::string(argv[2]) == "-h")) {
      options.command = TestClientCommand::Help;
      return options;
    }
    parse_mqttx_pub_options(options, argc, argv, 2);
    return options;
  }

  if (command_name == "subscribe") {
    options.command = TestClientCommand::Subscribe;
    parse_common_options(options, argc, argv, 2);
    return options;
  }

  if (command_name == "sub") {
    options.command = TestClientCommand::Subscribe;
    if (argc == 3 && is_help_flag(argv[2])) {
      options.command = TestClientCommand::Help;
      return options;
    }
    parse_mqttx_sub_options(options, argc, argv, 2);
    return options;
  }

  if (command_name == "simulate") {
    if (argc == 3 && is_help_flag(argv[2])) {
      options.command = TestClientCommand::Help;
      return options;
    }
    parse_mqttx_simulate_options(options, argc, argv, 2);
    return options;
  }

  if (command_name == "ls") {
    if (argc == 3 && is_help_flag(argv[2])) {
      options.command = TestClientCommand::Help;
      return options;
    }
    if (argc == 3 && (std::string(argv[2]) == "--scenarios" ||
                      std::string(argv[2]) == "-sc")) {
      options.command = TestClientCommand::Scenario;
      options.list_scenarios = true;
      return options;
    }
    throw std::invalid_argument(
        "ls command supports only --scenarios|-sc or --help");
  }

  if (command_name == "init") {
    if (argc == 3 && is_help_flag(argv[2])) {
      options.command = TestClientCommand::Help;
      return options;
    }
    options.command = TestClientCommand::Init;
    parse_common_options(options, argc, argv, 2);
    return options;
  }

  if (command_name == "check") {
    if (argc == 3 && is_help_flag(argv[2])) {
      options.command = TestClientCommand::Help;
      return options;
    }
    options.command = TestClientCommand::Check;
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

  if (command_name == "bench") {
    if (argc == 3 && std::string(argv[2]) == "--help") {
      options.command = TestClientCommand::Help;
      return options;
    }
    if (argc <= 2) {
      throw std::invalid_argument("bench command requires subcommand: conn|pub|sub");
    }

    const std::string bench_subcommand = argv[2];
    options.command = TestClientCommand::Scenario;
    if (bench_subcommand == "conn") {
      options.load_mode = "mass-connect";
    } else if (bench_subcommand == "pub") {
      options.load_mode = "publish-rate";
    } else if (bench_subcommand == "sub") {
      options.load_mode = "multi-subscribe";
    } else {
      throw std::invalid_argument("Unknown bench subcommand: " + bench_subcommand);
    }
    if (argc == 4 && std::string(argv[3]) == "--help") {
      options.command = TestClientCommand::Help;
      return options;
    }
    parse_bench_options(options, argc, argv, 3);
    return options;
  }

  if (command_name == "conn") {
    if (argc == 2 || (argc == 3 && is_help_flag(argv[2]))) {
      options.command = TestClientCommand::Help;
      return options;
    }
    throw std::invalid_argument("Command is recognized but not implemented yet: " +
                                command_name);
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
  "Top-level options:\n"
  "  --help         Show this help\n"
  "  --version, -v  Show executable version\n\n"
      "Commands:\n"
      "  connect        Connect using profile + CLI overrides and keep session open\n"
      "  publish|pub    Connect, publish one message, wait for QoS ACK flow, exit\n"
        "  subscribe|sub  Connect, subscribe, stream matching publishes, and optionally exit on message limit\n"
  "  conn           mqttx compatibility command stub (help-only)\n"
      "  bench          mqttx-compatible load benchmark entrypoint (conn|pub|sub)\n"
  "  simulate       mqttx-compatible simulation alias mapped to scenario runner\n"
  "  ls             List helpers (`ls --scenarios` mapped to scenario catalog)\n"
  "  init           Write default test-client options profile\n"
  "  check          Print compatibility/runtime capability summary\n"
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
      "  --subscribe-payload-format <raw|json|hex|base64|binary|protobuf|avro>\n"
      "  --subscribe-protobuf-path <path>\n"
      "  --subscribe-protobuf-message-name <name>\n"
      "  --subscribe-avsc-path <path>\n"
      "  --clean-output\n"
      "  --verbose-packets\n"
      "  --output-file <path>\n"
      "  --output-file-save <directory>\n"
      "  --append-output\n"
      "  --output-delimiter <text>\n"
      "  --output-format <template>\n"
      "  --message-limit <count>\n"
      "  --wait-timeout-ms <milliseconds>\n\n"
      "mqttx compatibility aliases (supported subset):\n"
      "  command: pub\n"
      "  publish: -t -m --message -q -r -d -s --stdin -M --multiline\n"
      "           --file-read -pf -e -ta -rt -cd -up --user-properties\n"
      "           -si -ct -f --format -Pp --protobuf-path\n"
      "           -Pmn --protobuf-message-name -Ap --avsc-path\n"
      "           -S --payload-size\n"
      "  subscribe: -t --topic -q --qos -nl --no_local -rap --retain-as-published\n"
      "             -rh --retain-handling -si --subscription-identifier\n"
      "             -up --user-properties -v --verbose --output-mode\n"
      "             --file-write --file-save --delimiter -f --format\n"
      "             -Pp --protobuf-path -Pmn --protobuf-message-name -Ap --avsc-path\n"
      "  connection: -h --hostname -p -i --no-clean -k --keepalive -u -P\n"
      "              -l --protocol --path -wh --ws-headers -rp --reconnect-period\n"
      "              -se --rcv-max --req-response-info --no-req-problem-info\n"
      "              -Cup --conn-user-properties -am -V --mqtt-version\n"
      "  will: -Wt -Wm -Wq -Wr -Wd -Wpf -We -Wct -Wrt -Wcd -Wup\n\n"
      "mqttx bench structure (non-TLS):\n"
      "  bench conn [options]\n"
      "  bench pub  [options]\n"
      "  bench sub  [options]\n"
      "  bench options: -c --count -i --interval -im --message-interval\n"
      "                 -L --limit -t --topic -I --client-id -v --verbose\n"
      "                 --split [delimiter] -S --payload-size\n"
      "                 plus mqttx pub/connection/will aliases above\n"
      "  templates: mqttx %i is supported and mapped to internal index placeholders\n"
      "  bench pub: uses persistent connections; --count controls connection pool size\n"
      "            and --limit 0 means unlimited publish loop\n"
      "  secure options (TLS/mqtts/wss) are intentionally unsupported\n\n"
      "scenario options:\n"
      "  --scenario <name>\n"
      "  --list-scenarios\n\n"
      "step32 load-mode options:\n"
      "  --load-mode <mass-connect|publish-rate|multi-subscribe>\n"
      "  --connection-count <count>\n"
      "  --connect-interval-ms <milliseconds>\n"
      "  --message-interval-ms <milliseconds>\n"
      "  --publish-limit <count>\n"
      "  --parallelism <count>\n"
      "  --topic-template <template-with-{index}>\n"
      "  --client-template <template-with-{index}>\n"
      "  --metrics-json\n\n"
      "simulate options:\n"
      "  -sc, --scenario <name>\n"
      "  -f, --file <path>\n"
      "  -c, --count <count>\n"
      "  -i, --interval <milliseconds>\n"
      "  -im, --message-interval <milliseconds>\n"
      "  -L, --limit <count>\n"
      "  -t, --topic <template>\n"
      "  -I, --client-id <template>\n"
      "  plus mqttx pub/connection/will aliases above\n"
      "  note: -f is accepted as simulation script selector and mapped\n"
      "        to scenario-runner script-file mode\n\n"
      "ls options:\n"
      "  -sc, --scenarios   List built-in scenarios and step32 load modes\n\n"
      "save-profile options:\n"
      "  --output <file>\n";
}

std::string test_client_version_text() {
  return "yahatestclient 0.1.0\n";
}

} // namespace mqtt
