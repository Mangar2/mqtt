#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

#include "test_client/test_client_cli.h"

namespace mqtt {
namespace {

template <std::size_t Size>
[[nodiscard]] int argc_of(const char *(&)[Size]) {
  return static_cast<int>(Size);
}

} // namespace

TEST_CASE("test_client_cli_publish_command_alias_pub_is_supported",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "pub", "-t", "topic/a", "-m",
                        "hello",          "-q",  "1",       "-r"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Publish);
  REQUIRE(options.overrides.size() == 4U);
  CHECK(options.overrides[0].first == "publish_topic");
  CHECK(options.overrides[0].second == "topic/a");
  CHECK(options.overrides[1].first == "publish_payload");
  CHECK(options.overrides[1].second == "hello");
  CHECK(options.overrides[2].first == "publish_qos");
  CHECK(options.overrides[2].second == "1");
  CHECK(options.overrides[3].first == "publish_retain");
  CHECK(options.overrides[3].second == "true");
}

TEST_CASE("test_client_cli_mqttx_publish_input_aliases_are_supported",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "pub",  "-t",    "topic/a",
                        "-s",             "-M",   "--file-read",
                        "payload.txt",    "-f",   "hex",
                        "-pf",            "1",    "-e",
                        "12",             "-ta",  "4",
                        "-rt",            "resp", "-cd",
                        "abcd",           "-up",  "k=v",
                        "-si",            "3",    "-ct",
                        "text/plain"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Publish);
  CHECK(options.overrides.size() == 13U);
}

TEST_CASE("test_client_cli_mqttx_connection_aliases_are_supported",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "pub",       "-t",          "topic/a",
      "-m",             "hello",     "-h",          "127.0.0.1",
      "-p",             "1883",      "-i",          "cid",
      "--no-clean",     "-k",        "30",          "-u",
      "user",           "-P",        "pass",        "-l",
      "ws",             "--path",    "/mqtt",       "-wh",
      "X-Test: one",    "-rp",       "500",         "--maximum-reconnect-times",
      "5",              "-se",       "11",          "--rcv-max",
      "20",             "--req-response-info", "--no-req-problem-info",
      "-Cup",           "a=b",       "-am",         "SCRAM-SHA-256",
      "-V",             "5.0"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Publish);
  CHECK_FALSE(options.overrides.empty());
}

TEST_CASE("test_client_cli_mqttx_will_aliases_are_supported", "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "pub",  "-t",    "topic/a", "-m",   "hello",
      "-Wt",            "wt",   "-Wm",   "wm",      "-Wq",  "1",
      "-Wr",            "-Wd",  "8",     "-Wpf",    "1",    "-We",
      "9",              "-Wct", "text",  "-Wrt",    "rt",   "-Wcd",
      "00ff",           "-Wup", "n=v"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Publish);
  CHECK_FALSE(options.overrides.empty());
}

TEST_CASE("test_client_cli_mqttx_version_alias_rejects_non_v5",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "pub", "-t", "topic/a", "-m",
                        "hello",          "-V",  "3.1.1"};

  CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
}

TEST_CASE("test_client_cli_pub_rejects_non_mqttx_host_flag",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "pub", "-t",     "topic/a",
                        "-m",             "hello", "--host", "127.0.0.1"};

  CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
}

TEST_CASE("test_client_cli_bench_pub_maps_to_scenario_publish_rate",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "bench", "pub", "-c", "100", "-L",
                        "200",            "-t",    "x/%i", "-I", "cid-%i"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Scenario);
  CHECK(options.load_mode == "publish-rate");
  CHECK(options.load_connection_count == 100U);
  CHECK(options.load_publish_limit == 200U);
  CHECK(options.load_topic_template == "x/{index}");
  CHECK(options.load_client_template == "cid-{index}");
}

TEST_CASE("test_client_cli_bench_conn_rejects_pub_only_flags",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "bench", "conn", "-m", "hello"};

  CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
}

TEST_CASE("test_client_cli_wp1_version_flags_are_supported",
          "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient", "--version"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Version);
  }
  {
    const char *argv[] = {"yahatestclient", "-v"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Version);
  }
}

TEST_CASE("test_client_cli_wp1_stub_commands_help_flow_is_supported",
          "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient", "conn", "--help"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Help);
  }
  {
    const char *argv[] = {"yahatestclient", "sub", "--help"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Help);
  }
  {
    const char *argv[] = {"yahatestclient", "simulate", "--help"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Help);
  }
  {
    const char *argv[] = {"yahatestclient", "ls", "--help"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Help);
  }
  {
    const char *argv[] = {"yahatestclient", "init", "--help"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Help);
  }
  {
    const char *argv[] = {"yahatestclient", "check", "--help"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Help);
  }
}

TEST_CASE("test_client_cli_wp1_stub_commands_without_help_fail",
          "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient", "conn", "--hostname", "127.0.0.1"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
  }
  {
    const char *argv[] = {"yahatestclient", "simulate", "-t", "a/b"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
  }
}

TEST_CASE("test_client_cli_wp1_bench_help_flows_are_supported",
          "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient", "bench", "--help"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Help);
  }
  {
    const char *argv[] = {"yahatestclient", "bench", "conn", "--help"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Help);
  }
  {
    const char *argv[] = {"yahatestclient", "bench", "pub", "--help"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Help);
  }
  {
    const char *argv[] = {"yahatestclient", "bench", "sub", "--help"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Help);
  }
}

TEST_CASE("test_client_cli_wp2_reconnect_alias_maximun_is_supported",
          "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient", "pub", "-t", "topic/a", "-m",
                          "hello",          "--maximun-reconnect-times", "2"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Publish);
    bool found_alias_override = false;
    for (const auto &entry : options.overrides) {
      if (entry.first == "maximum_reconnect_times" && entry.second == "2") {
        found_alias_override = true;
        break;
      }
    }
    CHECK(found_alias_override);
  }

  {
    const char *argv[] = {"yahatestclient", "bench", "pub", "-t", "topic/%i",
                          "-m",             "hello", "--maximun-reconnect-times", "3"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Scenario);
    CHECK(options.load_mode == "publish-rate");
    bool found_alias_override = false;
    for (const auto &entry : options.overrides) {
      if (entry.first == "maximum_reconnect_times" && entry.second == "3") {
        found_alias_override = true;
        break;
      }
    }
    CHECK(found_alias_override);
  }
}

TEST_CASE("test_client_cli_wp2_pub_rejects_not_implemented_debug_save_load_options",
          "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient", "pub", "-t", "topic/a", "-m",
                          "hello",          "--debug"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv),
                    std::invalid_argument);
  }
  {
    const char *argv[] = {"yahatestclient", "pub", "-t", "topic/a", "-m",
                          "hello",          "--save-options"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv),
                    std::invalid_argument);
  }
  {
    const char *argv[] = {"yahatestclient", "pub", "-t", "topic/a", "-m",
                          "hello",          "--load-options"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv),
                    std::invalid_argument);
  }
}

TEST_CASE("test_client_cli_wp2_bench_rejects_not_implemented_debug_save_load_options",
          "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient", "bench", "pub", "-t", "topic/%i",
                          "-m",             "hello", "--debug"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv),
                    std::invalid_argument);
  }
  {
    const char *argv[] = {"yahatestclient", "bench", "pub", "-t", "topic/%i",
                          "-m",             "hello", "--save-options"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv),
                    std::invalid_argument);
  }
  {
    const char *argv[] = {"yahatestclient", "bench", "pub", "-t", "topic/%i",
                          "-m",             "hello", "--load-options"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv),
                    std::invalid_argument);
  }
}

TEST_CASE("test_client_cli_wp3_bench_verbose_is_not_metrics_json",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "bench", "pub", "-t", "topic/%i",
                        "-m",             "hello", "-v", "-c", "1", "-L", "2"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Scenario);
  CHECK(options.load_mode == "publish-rate");
  CHECK(options.load_verbose);
  CHECK_FALSE(options.load_metrics_json);
}

TEST_CASE("test_client_cli_wp3_bench_split_and_payload_size_are_parsed",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "bench", "pub", "-t", "topic/%i",
                        "-m",             "a|bb|ccc", "--split", "|",
                        "-S",             "8", "-c", "1", "-L", "3"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Scenario);
  CHECK(options.load_mode == "publish-rate");
  CHECK(options.load_split_enabled);
  CHECK(options.load_split_delimiter == "|");
  CHECK(options.load_payload_size == 8U);
}

TEST_CASE("test_client_cli_wp3_bench_limit_zero_is_parsed",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "bench", "pub", "-t", "topic/%i",
                        "-m",             "hello", "-c", "2", "-L", "0"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Scenario);
  CHECK(options.load_mode == "publish-rate");
  CHECK(options.load_connection_count == 2U);
  CHECK(options.load_publish_limit == 0U);
}

TEST_CASE("test_client_cli_wp4_pub_payload_schema_and_size_options_are_parsed",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "pub", "-t", "topic/a", "-m", "hello",
                        "-f",             "protobuf", "-Pp", "schema.proto",
                        "-Pmn",           "Envelope", "-S", "16"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Publish);

  bool has_schema_path = false;
  bool has_message_name = false;
  bool has_payload_size = false;
  for (const auto &entry : options.overrides) {
    if (entry.first == "publish_protobuf_path" && entry.second == "schema.proto") {
      has_schema_path = true;
    }
    if (entry.first == "publish_protobuf_message_name" &&
        entry.second == "Envelope") {
      has_message_name = true;
    }
    if (entry.first == "publish_payload_size" && entry.second == "16") {
      has_payload_size = true;
    }
  }

  CHECK(has_schema_path);
  CHECK(has_message_name);
  CHECK(has_payload_size);
}

TEST_CASE("test_client_cli_wp4_bench_pub_publish_properties_and_schema_flags_are_parsed",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "bench", "pub", "-t", "topic/%i", "-m", "payload",
      "-d",             "-pf",   "1",   "-e", "12",       "-ta", "5",
      "-rt",            "reply", "-cd", "abcd", "-si",    "9",
      "-ct",            "text/plain", "-up", "k=v", "-f", "avro",
      "-Ap",            "schema.avsc", "-c", "1", "-L", "1"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Scenario);
  CHECK(options.load_mode == "publish-rate");

  bool has_dup = false;
  bool has_payload_format = false;
  bool has_expiry = false;
  bool has_alias = false;
  bool has_response_topic = false;
  bool has_correlation = false;
  bool has_subscription_identifier = false;
  bool has_content_type = false;
  bool has_user_property = false;
  bool has_encoding = false;
  bool has_avsc = false;
  for (const auto &entry : options.overrides) {
    has_dup = has_dup ||
              (entry.first == "publish_dup" && entry.second == "true");
    has_payload_format = has_payload_format ||
                         (entry.first == "publish_payload_format_indicator" &&
                          entry.second == "1");
    has_expiry = has_expiry ||
                 (entry.first == "publish_message_expiry_interval_seconds" &&
                  entry.second == "12");
    has_alias = has_alias ||
                (entry.first == "publish_topic_alias" && entry.second == "5");
    has_response_topic = has_response_topic ||
                         (entry.first == "publish_response_topic" &&
                          entry.second == "reply");
    has_correlation = has_correlation ||
                      (entry.first == "publish_correlation_data" &&
                       entry.second == "abcd");
    has_subscription_identifier = has_subscription_identifier ||
                                  (entry.first == "publish_subscription_identifier" &&
                                   entry.second == "9");
    has_content_type = has_content_type ||
                       (entry.first == "publish_content_type" &&
                        entry.second == "text/plain");
    has_user_property = has_user_property ||
                        (entry.first == "publish_user_property" &&
                         entry.second == "k=v");
    has_encoding = has_encoding ||
                   (entry.first == "publish_payload_encoding" &&
                    entry.second == "avro");
    has_avsc = has_avsc ||
               (entry.first == "publish_avsc_path" &&
                entry.second == "schema.avsc");
  }

  CHECK(has_dup);
  CHECK(has_payload_format);
  CHECK(has_expiry);
  CHECK(has_alias);
  CHECK(has_response_topic);
  CHECK(has_correlation);
  CHECK(has_subscription_identifier);
  CHECK(has_content_type);
  CHECK(has_user_property);
  CHECK(has_encoding);
  CHECK(has_avsc);
}

TEST_CASE("test_client_cli_wp5_sub_command_maps_mqttx_aliases",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "sub", "-t", "topic/a", "-q", "1",
      "-nl",            "true", "-rap", "true", "-rh", "1",
      "-si",            "7", "-up", "k=v", "--output-mode", "clean",
      "--file-write",   "sub.log", "--delimiter", "|", "-f", "hex"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Subscribe);

  bool has_sub_entry = false;
  bool has_sub_id = false;
  bool has_user_property = false;
  bool has_clean_output = false;
  bool has_file_write = false;
  bool has_append = false;
  bool has_delimiter = false;
  bool has_payload_format = false;
  for (const auto &entry : options.overrides) {
    has_sub_entry = has_sub_entry ||
                    (entry.first == "subscribe_entry" &&
                     entry.second == "topic/a|1|true|true|1");
    has_sub_id = has_sub_id ||
                 (entry.first == "subscribe_identifier" &&
                  entry.second == "7");
    has_user_property = has_user_property ||
                        (entry.first == "subscribe_user_property" &&
                         entry.second == "k=v");
    has_clean_output = has_clean_output ||
                       (entry.first == "subscribe_clean_output" &&
                        entry.second == "true");
    has_file_write = has_file_write ||
                     (entry.first == "subscribe_output_file" &&
                      entry.second == "sub.log");
    has_append = has_append ||
                 (entry.first == "subscribe_output_append" &&
                  entry.second == "true");
    has_delimiter = has_delimiter ||
                    (entry.first == "subscribe_output_delimiter" &&
                     entry.second == "|");
    has_payload_format = has_payload_format ||
                         (entry.first == "subscribe_payload_format" &&
                          entry.second == "hex");
  }

  CHECK(has_sub_entry);
  CHECK(has_sub_id);
  CHECK(has_user_property);
  CHECK(has_clean_output);
  CHECK(has_file_write);
  CHECK(has_append);
  CHECK(has_delimiter);
  CHECK(has_payload_format);
}

TEST_CASE("test_client_cli_wp5_bench_sub_option_semantics_are_parsed",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "bench", "sub", "-t", "topic/%i", "-q", "2",
      "-nl",            "true", "-rap", "true", "-rh", "1",
      "-si",            "9", "-c", "2", "--maximum-reconnect-times", "0"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Scenario);
  CHECK(options.load_mode == "multi-subscribe");
  CHECK(options.load_subscribe_qos == 2U);
  CHECK(options.load_subscribe_no_local);
  CHECK(options.load_subscribe_retain_as_published);
  CHECK(options.load_subscribe_retain_handling == 1U);
  CHECK(options.load_subscribe_identifier_set);
  CHECK(options.load_subscribe_identifier == 9U);
}

TEST_CASE("test_client_cli_wp6_simulate_maps_to_step32_load_mode",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "simulate", "-sc", "mass-connect", "-c", "3",
      "-i",             "2",        "-im", "1",            "-L", "5",
      "-t",             "sim/%i",    "-I",  "sim-client-%i"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Scenario);
  CHECK(options.load_mode == "mass-connect");
  CHECK(options.load_connection_count == 3U);
  CHECK(options.load_connect_interval_ms == 2U);
  CHECK(options.load_message_interval_ms == 1U);
  CHECK(options.load_publish_limit == 5U);
  CHECK(options.load_topic_template == "sim/{index}");
  CHECK(options.load_client_template == "sim-client-{index}");
}

TEST_CASE("test_client_cli_wp6_ls_scenarios_maps_to_scenario_list_mode",
          "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient", "ls", "--scenarios"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Scenario);
    CHECK(options.list_scenarios);
  }
  {
    const char *argv[] = {"yahatestclient", "ls", "-sc"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Scenario);
    CHECK(options.list_scenarios);
  }
}

TEST_CASE("test_client_cli_wp6_init_and_check_commands_are_parsed",
          "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient", "init", "--output", "init.ini"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Init);
    CHECK(options.output_path == "init.ini");
  }
  {
    const char *argv[] = {"yahatestclient", "check"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Check);
  }
}

TEST_CASE("test_client_cli_mqttx_pub_accepts_avsc_alias_in_pub_mode",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "pub", "-t", "topic/a", "-m",
                        "hello",          "-f",  "avro", "-Ap",
                        "schema.avsc"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Publish);

  bool has_avsc = false;
  for (const auto &entry : options.overrides) {
    if (entry.first == "publish_avsc_path" && entry.second == "schema.avsc") {
      has_avsc = true;
      break;
    }
  }
  CHECK(has_avsc);
}

TEST_CASE("test_client_cli_mqttx_pub_rejects_secure_tls_flags",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "pub", "-t", "topic/a", "-m",
                        "hello",          "--key", "client.key"};

  CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
}

TEST_CASE("test_client_cli_publish_command_with_short_aliases_covers_common_parser_paths",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "publish", "-t",   "topic/a", "-m",      "hello",
      "-q",             "1",       "-r",   "-d",      "-s",      "-M",
      "--file-read",    "msg.txt", "-pf",  "1",       "-e",      "9",
      "-ta",            "3",       "-rt",  "reply",   "-cd",     "abcd",
      "-up",            "k=v",     "-si",  "7",       "-ct",     "text/plain",
      "-f",             "json",    "-h",   "127.0.0.1", "-p",    "1883",
      "-i",             "cid",     "--no-clean", "-k", "20",     "-u",
      "user",           "-P",      "pass", "-l",      "ws",      "--path",
      "/mqtt",          "-wh",     "X-A: b", "-rp",   "100",     "-se",
      "12",             "--rcv-max", "50", "--req-response-info",
      "--no-req-problem-info", "-Cup", "p=v", "-Wt", "wt",      "-Wm",
      "wm",             "-Wq",     "1",   "-Wr",      "-Wd",      "8",
      "-Wpf",           "1",       "-We", "5",        "-Wct",     "application/json",
      "-Wrt",           "wrt",     "-Wcd", "00ff",    "-Wup",     "wk=wv",
      "-am",            "SCRAM-SHA-256", "-V", "5.0"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Publish);
  CHECK(options.overrides.size() >= 35U);
}

TEST_CASE("test_client_cli_publish_command_with_long_options_covers_common_parser_paths",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "publish",
      "--host", "127.0.0.1", "--port", "1883", "--transport", "ws", "--ws-path", "/mqtt",
      "--ws-header", "X-Test: one", "--client-id", "cid-long", "--clean-start", "false",
      "--keep-alive-seconds", "15", "--username", "user", "--password", "pass",
      "--reconnect-period-ms", "100", "--maximum-reconnect-times", "2",
      "--session-expiry-interval-seconds", "10", "--receive-maximum", "20",
      "--maximum-packet-size", "2048", "--topic-alias-maximum", "5",
      "--request-response-information", "true", "--request-problem-information", "false",
      "--connect-user-property", "ck=cv", "--authentication-method", "SCRAM-SHA-256",
      "--authentication-data", "token", "--will-topic", "will/topic", "--will-payload", "will-body",
      "--will-qos", "1", "--will-retain", "true", "--will-delay-interval-seconds", "6",
      "--will-payload-format-indicator", "1", "--will-message-expiry-interval-seconds", "7",
      "--will-content-type", "text/plain", "--will-response-topic", "will/resp",
      "--will-correlation-data", "00ff", "--will-user-property", "wk=wv",
      "--topic", "topic/long", "--qos", "1", "--retain", "true", "--dup", "false",
      "--payload", "payload", "--payload-stdin", "--payload-stdin-multiline",
      "--payload-file", "payload.txt", "--payload-encoding", "json",
      "--payload-format-indicator", "1", "--message-expiry-interval-seconds", "4",
      "--topic-alias", "3", "--response-topic", "reply/topic", "--correlation-data", "abcd",
      "--correlation-data-encoding", "base64", "--subscription-identifier", "9",
      "--content-type", "application/json", "--publish-user-property", "pk=pv",
      "--subscription", "home/+/state|1|false|false|0", "--subscribe-identifier", "11",
      "--subscribe-user-property", "sk=sv", "--subscribe-payload-format", "hex",
      "--subscribe-protobuf-path", "schema.proto", "--subscribe-protobuf-message-name", "Envelope",
      "--subscribe-avsc-path", "schema.avsc", "--clean-output", "--verbose-packets",
      "--output-file", "sub.log", "--output-file-save", "outdir", "--append-output",
      "--output-delimiter", "|", "--output-format", "{topic}:{payload}",
      "--message-limit", "2", "--wait-timeout-ms", "1000"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Publish);
  CHECK(options.overrides.size() >= 60U);
}

TEST_CASE("test_client_cli_publish_command_covers_remaining_common_error_and_alias_paths",
          "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient", "publish", "-V", "3.1.1"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
  }
  {
    const char *argv[] = {"yahatestclient", "publish", "--maximun-reconnect-times", "4"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Publish);
    bool has_max = false;
    for (const auto &entry : options.overrides) {
      if (entry.first == "maximum_reconnect_times" && entry.second == "4") {
        has_max = true;
        break;
      }
    }
    CHECK(has_max);
  }
  {
    const char *argv[] = {"yahatestclient", "publish", "--save-options"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
  }
  {
    const char *argv[] = {"yahatestclient", "publish", "--load-options"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
  }
  {
    const char *argv[] = {"yahatestclient", "publish", "--debug"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
  }
  {
    const char *argv[] = {"yahatestclient", "publish", "--payload-format-indicator", "1"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Publish);
    bool has_payload_format = false;
    for (const auto &entry : options.overrides) {
      if (entry.first == "publish_payload_format_indicator" && entry.second == "1") {
        has_payload_format = true;
        break;
      }
    }
    CHECK(has_payload_format);
  }
}

TEST_CASE("test_client_cli_pub_help_shortcuts_return_help", "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient", "pub", "--help"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Help);
  }
  {
    const char *argv[] = {"yahatestclient", "pub", "-h"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Help);
  }
}

TEST_CASE("test_client_cli_ls_and_bench_error_paths", "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient", "ls", "invalid"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
  }
  {
    const char *argv[] = {"yahatestclient", "bench"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
  }
  {
    const char *argv[] = {"yahatestclient", "bench", "noop"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
  }
}

TEST_CASE("test_client_cli_mqttx_pub_long_alias_variants_are_supported",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "pub",
      "-t", "topic/a", "--message", "hello", "-q", "1",
      "-lm", "--message-expiry-interval", "12", "--topic-alias", "5",
      "--response-topic", "reply", "--correlation-data", "abcd",
      "--user-properties", "k=v", "--subscription-identifier", "3",
      "--content-type", "text/plain", "--mqtt-version", "5",
      "--hostname", "127.0.0.1", "--port", "1883", "--client-id", "cid",
      "--keepalive", "30", "--username", "user", "--password", "pass",
      "--protocol", "ws", "--ws-headers", "X-A: b", "--reconnect-period", "250",
      "--session-expiry-interval", "11", "--receive-maximum", "20",
      "--conn-user-properties", "ck=cv", "--will-topic", "wt",
      "--will-message", "wm", "--will-qos", "1", "--will-retain",
      "--will-delay-interval", "8", "--will-payload-format-indicator", "1",
      "--will-message-expiry-interval", "9", "--will-content-type", "text/plain",
      "--will-response-topic", "wrt", "--will-correlation-data", "00ff",
      "--will-user-properties", "wk=wv", "--authentication-method", "SCRAM-SHA-256"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Publish);
  CHECK(options.overrides.size() >= 25U);
}

TEST_CASE("test_client_cli_mqttx_pub_long_payload_format_indicator_is_supported",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "pub", "--topic", "topic/a", "--message", "hello",
      "--payload-format-indicator", "1"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Publish);

  bool has_payload_format = false;
  for (const auto &entry : options.overrides) {
    if (entry.first == "publish_payload_format_indicator" && entry.second == "1") {
      has_payload_format = true;
      break;
    }
  }
  CHECK(has_payload_format);
}

TEST_CASE("test_client_cli_mqttx_sub_long_alias_variants_are_supported",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "sub",
      "--topic", "topic/a", "--qos", "1", "--no_local", "true",
      "--retain-as-published", "true", "--retain-handling", "1",
      "--subscription-identifier", "9", "--user-properties", "k=v",
      "--verbose", "--output-mode", "clean", "--file-write", "out.log",
      "--file-save", "saved.log", "--delimiter", "|", "--format", "hex",
      "--protobuf-path", "schema.proto", "--protobuf-message-name", "Envelope",
      "--avsc-path", "schema.avsc"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Subscribe);
  CHECK(options.overrides.size() >= 10U);
}

TEST_CASE("test_client_cli_scenario_list_scenarios_flag_is_parsed",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "scenario", "--list-scenarios"};
  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Scenario);
  CHECK(options.list_scenarios);
}

TEST_CASE("test_client_cli_publish_payload_format_indicator_long_flag_is_parsed",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "publish",
                        "--payload-format-indicator", "1"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Publish);
  REQUIRE(options.overrides.size() == 1U);
  CHECK(options.overrides[0].first == "publish_payload_format_indicator");
  CHECK(options.overrides[0].second == "1");
}

TEST_CASE("test_client_cli_branch_sweep_executes_many_option_paths",
          "[test_client][cli]") {
  auto run_case = [](const std::vector<std::string> &args) {
    std::vector<const char *> argv;
    argv.reserve(args.size());
    for (const auto &item : args) {
      argv.push_back(item.c_str());
    }

    try {
      (void)parse_test_client_cli(static_cast<int>(argv.size()), argv.data());
    } catch (const std::invalid_argument &) {
      // Sweep includes unsupported combinations intentionally to hit error branches.
    }
  };

  run_case({"yahatestclient", "connect", "--host", "127.0.0.1", "--port", "1883",
            "--client-id", "cid", "--clean-start", "false", "--keep-alive-seconds", "20",
            "--username", "u", "--password", "p", "--reconnect-period-ms", "100"});

  run_case({"yahatestclient", "publish", "--topic", "topic/a", "--payload", "x",
            "--payload-format-indicator", "1", "--message-expiry-interval-seconds", "5",
            "--topic-alias", "2", "--response-topic", "r/t", "--correlation-data", "aa",
            "--subscription-identifier", "7", "--content-type", "text/plain"});

  run_case({"yahatestclient", "subscribe", "--subscription", "x/+/y|1|false|false|0",
            "--subscribe-identifier", "3", "--subscribe-user-property", "k=v",
            "--output-file", "out.log", "--append-output", "--output-delimiter", "|",
            "--output-format", "{topic}:{payload}", "--message-limit", "2"});

  run_case({"yahatestclient", "pub", "--topic", "topic/a", "--message", "hello",
            "--payload-format-indicator", "1", "--message-expiry-interval", "9",
            "--topic-alias", "4", "--response-topic", "reply", "--correlation-data", "abcd",
            "--user-properties", "k=v", "--subscription-identifier", "3", "--content-type", "text",
            "--hostname", "127.0.0.1", "--port", "1883", "--client-id", "cid",
            "--keepalive", "30", "--username", "user", "--password", "pass",
            "--protocol", "ws", "--ws-headers", "X:1", "--reconnect-period", "500",
            "--session-expiry-interval", "11", "--receive-maximum", "20", "--conn-user-properties", "a=b",
            "--will-topic", "wt", "--will-message", "wm", "--will-qos", "1", "--will-retain",
            "--will-delay-interval", "8", "--will-payload-format-indicator", "1",
            "--will-message-expiry-interval", "9", "--will-content-type", "text/plain",
            "--will-response-topic", "wrt", "--will-correlation-data", "00ff",
            "--will-user-properties", "wk=wv", "--mqtt-version", "5"});

  run_case({"yahatestclient", "sub", "--topic", "topic/a", "--qos", "1", "--no_local", "true",
            "--retain-as-published", "true", "--retain-handling", "1",
            "--subscription-identifier", "9", "--user-properties", "k=v", "--verbose",
            "--output-mode", "clean", "--file-write", "out.log", "--file-save", "saved.log",
            "--delimiter", "|", "--format", "hex", "--protobuf-path", "schema.proto",
            "--protobuf-message-name", "Envelope", "--avsc-path", "schema.avsc"});

  run_case({"yahatestclient", "simulate", "--scenario", "mass-connect", "--file", "sim.txt",
            "-c", "2", "-i", "1", "-im", "1", "-L", "3", "-t", "sim/%i", "-I", "cid-%i"});

  run_case({"yahatestclient", "scenario", "--list-scenarios"});
  run_case({"yahatestclient", "ls", "invalid"});
  run_case({"yahatestclient", "bench"});
  run_case({"yahatestclient", "bench", "noop"});
  run_case({"yahatestclient", "bench", "pub", "--save-options"});
  run_case({"yahatestclient", "bench", "sub", "--load-options"});
  run_case({"yahatestclient", "pub", "--debug"});
  run_case({"yahatestclient", "pub", "--key", "client.key"});
}

TEST_CASE("test_client_cli_mqttx_sub_compact_qos_and_default_output_mode_are_supported",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "sub", "--topic", "topic/a", "-q2",
      "--output-mode", "default", "--hostname", "127.0.0.1"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Subscribe);

  bool has_default_mode = false;
  bool has_compact_qos = false;
  for (const auto &entry : options.overrides) {
    has_default_mode = has_default_mode ||
                       (entry.first == "subscribe_clean_output" &&
                        entry.second == "false");
    has_compact_qos = has_compact_qos ||
                      (entry.first == "subscribe_entry" &&
                       entry.second.find("|2|") != std::string::npos);
  }
  CHECK(has_default_mode);
  CHECK(has_compact_qos);
}

TEST_CASE("test_client_cli_mqttx_sub_delimiter_without_value_uses_default",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "sub", "--topic", "topic/a", "--delimiter"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Subscribe);

  bool has_default_delimiter = false;
  for (const auto &entry : options.overrides) {
    if (entry.first == "subscribe_output_delimiter" && entry.second == "\n") {
      has_default_delimiter = true;
      break;
    }
  }
  CHECK(has_default_delimiter);
}

TEST_CASE("test_client_cli_mqttx_sub_boolean_flags_without_values_enable_true",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "sub", "--topic", "topic/a", "--no_local",
      "--retain-as-published"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Subscribe);

  bool has_true_flags = false;
  for (const auto &entry : options.overrides) {
    if (entry.first == "subscribe_entry" &&
        entry.second.find("|true|true|") != std::string::npos) {
      has_true_flags = true;
      break;
    }
  }
  CHECK(has_true_flags);
}

TEST_CASE("test_client_cli_mqttx_sub_rejects_secure_tls_flags",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "sub", "--topic", "topic/a", "--key", "client.key"};

  CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
}

TEST_CASE("test_client_cli_bench_pub_extended_option_sweep_covers_parser_branches",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "bench", "pub", "-c", "4", "-i", "10", "-im", "5",
      "-L", "9", "-t", "bench/%i", "-I", "cid-%i", "-m", "payload", "-q", "1",
      "-q2", "-r", "-d", "-pf", "1", "-e", "3", "-ta", "2", "-rt", "reply",
      "-cd", "ab", "-up", "k=v", "-si", "8", "-ct", "text/plain", "-v",
      "--metrics-json", "-h", "127.0.0.1", "-p", "1883", "--no-clean", "-k", "20",
      "-u", "user", "-P", "pass", "-l", "ws", "--path", "/mqtt", "-wh", "X:1",
      "-rp", "200", "--maximum-reconnect-times", "7", "-se", "11", "--rcv-max", "30",
      "--maximum-packet-size", "2048", "--topic-alias-maximum", "5", "--req-response-info",
      "--no-req-problem-info", "-Cup", "ck=cv", "-Wt", "wt", "-Wm", "wm", "-Wq", "1",
      "-Wr", "-Wd", "6", "-Wpf", "1", "-We", "9", "-Wct", "application/json",
      "-Wrt", "wrt", "-Wcd", "00ff", "-Wup", "wk=wv", "--file-read", "payload.txt",
      "-f", "avro", "-Pp", "schema.proto", "-Pmn", "Envelope", "-Ap", "schema.avsc",
      "--split", "|", "-S", "42", "-V", "5"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Scenario);
  CHECK(options.load_mode == "publish-rate");
  CHECK(options.load_connect_interval_ms == 10U);
  CHECK(options.load_message_interval_ms == 5U);
  CHECK(options.load_split_enabled);
  CHECK(options.load_split_delimiter == "|");
  CHECK(options.load_metrics_json);
  CHECK(options.load_payload_size == 42U);
}

TEST_CASE("test_client_cli_bench_sub_and_conn_user_property_paths_are_supported",
          "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient", "bench", "sub", "-t", "topic/%i", "-q2",
                          "-nl", "false", "-rap", "0", "-rh", "2", "-up", "s=v",
                          "-si", "7", "-c", "2"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Scenario);
    CHECK(options.load_mode == "multi-subscribe");
    CHECK(options.load_subscribe_qos == 2U);
    CHECK_FALSE(options.load_subscribe_no_local);
    CHECK_FALSE(options.load_subscribe_retain_as_published);
    CHECK(options.load_subscribe_retain_handling == 2U);
  }

  {
    const char *argv[] = {"yahatestclient", "bench", "conn", "-c", "3", "-i", "10",
                          "-up", "ck=cv", "-h", "127.0.0.1", "-k", "15"};
    const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
    CHECK(options.command == TestClientCommand::Scenario);
    CHECK(options.load_mode == "mass-connect");
    bool has_conn_property = false;
    for (const auto &entry : options.overrides) {
      if (entry.first == "connect_user_property" && entry.second == "ck=cv") {
        has_conn_property = true;
        break;
      }
    }
    CHECK(has_conn_property);
  }
}

TEST_CASE("test_client_cli_bench_and_sub_boolean_parsers_reject_invalid_literals",
          "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient", "bench", "sub", "-t", "topic/%i", "-nl",
                          "maybe"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
  }
  {
    const char *argv[] = {"yahatestclient", "sub", "-t", "topic/a", "--retain-as-published",
                          "maybe"};
    CHECK_THROWS_AS(parse_test_client_cli(argc_of(argv), argv), std::invalid_argument);
  }
}

TEST_CASE("test_client_cli_sub_extended_connection_options_are_supported",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "sub", "-t", "topic/a", "--message-limit", "4",
      "--wait-timeout-ms", "1000", "-h", "127.0.0.1", "-p", "1883", "-i", "cid",
      "--no-clean", "-k", "30", "-u", "user", "-P", "pass", "--path", "/mqtt",
      "-wh", "X-Test: one", "-rp", "500", "--maximum-reconnect-times", "3"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Subscribe);
  bool has_limit = false;
  bool has_timeout = false;
  bool has_client_id = false;
  bool has_reconnect = false;
  for (const auto &entry : options.overrides) {
    has_limit = has_limit ||
                (entry.first == "subscribe_message_limit" && entry.second == "4");
    has_timeout = has_timeout ||
                  (entry.first == "subscribe_wait_timeout_ms" && entry.second == "1000");
    has_client_id = has_client_id ||
                    (entry.first == "client_id" && entry.second == "cid");
    has_reconnect = has_reconnect ||
                    (entry.first == "maximum_reconnect_times" && entry.second == "3");
  }
  CHECK(has_limit);
  CHECK(has_timeout);
  CHECK(has_client_id);
  CHECK(has_reconnect);
}

TEST_CASE("test_client_cli_simulate_extended_option_sweep_covers_parser_branches",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient", "simulate", "-f", "script.yaml", "-c", "2", "-i", "1",
      "-im", "1", "-L", "2", "-t", "sim/%i", "-I", "sim-%i", "-m", "hello",
      "-q", "1", "-q2", "-r", "-v", "--metrics-json", "-h", "127.0.0.1",
      "-p", "1883", "--no-clean", "-k", "20", "-u", "user", "-P", "pass",
      "-l", "ws", "--path", "/mqtt", "-wh", "X-A: b", "-rp", "300",
      "--maximum-reconnect-times", "4", "-se", "9", "--rcv-max", "10",
      "--maximum-packet-size", "1024", "--topic-alias-maximum", "6", "--req-response-info",
      "--no-req-problem-info", "-Cup", "ck=cv", "-Wt", "wt", "-Wm", "wm",
      "-Wq", "1", "-Wr", "-Wd", "5", "-Wpf", "1", "-We", "6", "-Wct", "text/plain",
      "-Wrt", "wrt", "-Wcd", "00ff", "-Wup", "wk=wv", "--format", "json",
      "-Pp", "schema.proto", "-Pmn", "Envelope", "-Ap", "schema.avsc", "-S", "16",
      "-am", "SCRAM-SHA-256", "-V", "5.0"};

  const TestClientCliOptions options = parse_test_client_cli(argc_of(argv), argv);
  CHECK(options.command == TestClientCommand::Scenario);
  CHECK(options.scenario_name == "script-file:script.yaml");
  CHECK(options.load_verbose);
  CHECK(options.load_metrics_json);
  CHECK(options.load_payload_size == 16U);
}

} // namespace mqtt
