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
    const char *argv[] = {"yahatestclient", "sub", "-t", "a/b"};
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

} // namespace mqtt
