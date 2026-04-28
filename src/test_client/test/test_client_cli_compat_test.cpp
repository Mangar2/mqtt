#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

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

} // namespace mqtt
