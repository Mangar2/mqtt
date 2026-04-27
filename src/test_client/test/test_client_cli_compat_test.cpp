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

} // namespace mqtt
