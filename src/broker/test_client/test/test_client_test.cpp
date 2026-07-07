#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "test_client/test_client_cli.h"
#include "test_client/test_client_profile.h"

namespace mqtt {

namespace {

std::string test_profile_ini_path(const std::string &file_name) {
  const std::filesystem::path base_directory =
      std::filesystem::path("test") / "tmp" / "test_client_profile";
  std::error_code error_code;
  std::filesystem::create_directories(base_directory, error_code);
  return (base_directory / file_name).string();
}

} // namespace

TEST_CASE("test_client_profile_defaults_validate", "[test_client][profile]") {
  const TestClientProfile default_profile;
  CHECK_NOTHROW(validate_test_client_profile_or_throw(default_profile));
}

TEST_CASE("test_client_profile_save_and_load_roundtrip_preserves_values",
          "[test_client][profile]") {
  TestClientProfile profile;
  profile.host = "broker.local";
  profile.port = 9001U;
  profile.transport = TestClientTransport::Ws;
  profile.ws_path = "/custom/mqtt";
  profile.ws_headers = {"X-Test: one", "X-Test: two"};
  profile.client_id = "step27-client";
  profile.clean_start = false;
  profile.keep_alive_seconds = 45U;
  profile.username = std::string{"alice"};
  profile.password = std::string{"secret"};
  profile.reconnect_period_ms = 2500U;
  profile.maximum_reconnect_times = 7U;

  const std::string path =
      test_profile_ini_path("test-client-profile-roundtrip.ini");
  save_test_client_profile_to_file(path, profile);

  const TestClientProfile loaded_profile =
      load_test_client_profile_from_file(path);

  CHECK(loaded_profile.host == "broker.local");
  CHECK(loaded_profile.port == 9001U);
  CHECK(loaded_profile.transport == TestClientTransport::Ws);
  CHECK(loaded_profile.ws_path == "/custom/mqtt");
  REQUIRE(loaded_profile.ws_headers.size() == 2U);
  CHECK(loaded_profile.ws_headers[0] == "X-Test: one");
  CHECK(loaded_profile.ws_headers[1] == "X-Test: two");
  CHECK(loaded_profile.client_id == "step27-client");
  CHECK_FALSE(loaded_profile.clean_start);
  CHECK(loaded_profile.keep_alive_seconds == 45U);
  REQUIRE(loaded_profile.username.has_value());
  CHECK(*loaded_profile.username == "alice");
  REQUIRE(loaded_profile.password.has_value());
  CHECK(*loaded_profile.password == "secret");
  CHECK(loaded_profile.reconnect_period_ms == 2500U);
  CHECK(loaded_profile.maximum_reconnect_times == 7U);
}

TEST_CASE("test_client_profile_load_rejects_unknown_key",
          "[test_client][profile]") {
  const std::string path =
      test_profile_ini_path("test-client-profile-invalid.ini");
  std::ofstream file(path, std::ios::trunc);
  REQUIRE(file.is_open());
  file << "host=127.0.0.1\n";
  file << "port=1883\n";
  file << "unsupported_key=value\n";
  file.close();

  CHECK_THROWS_AS(load_test_client_profile_from_file(path),
                  std::invalid_argument);
}

TEST_CASE("test_client_profile_validate_rejects_invalid_ws_path",
          "[test_client][profile]") {
  TestClientProfile profile;
  profile.transport = TestClientTransport::Ws;
  profile.ws_path = "mqtt";

  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);
}

TEST_CASE("test_client_profile_validation_and_override_error_paths",
          "[test_client][profile]") {
  TestClientProfile profile;

  profile.host.clear();
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.port = 0U;
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.client_id.clear();
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.keep_alive_seconds = 0U;
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.reconnect_period_ms = 0U;
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.username = std::string{};
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.password = std::string{};
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  TestClientProfile override_profile;
  CHECK_THROWS_AS(apply_profile_override(override_profile, "clean_start", "bad"),
                  std::invalid_argument);
  CHECK_THROWS_AS(apply_profile_override(override_profile, "port", "abc"),
                  std::invalid_argument);
  CHECK_THROWS_AS(apply_profile_override(override_profile, "port", "0"),
                  std::invalid_argument);
  CHECK_THROWS_AS(
      apply_profile_override(override_profile, "keep_alive_seconds", "70000"),
      std::invalid_argument);
  CHECK_THROWS_AS(apply_profile_override(override_profile, "transport", "invalid"),
                  std::invalid_argument);

  const std::string missing_value_path =
      test_profile_ini_path("test-client-profile-missing-value.ini");
  {
    std::ofstream file(missing_value_path, std::ios::trunc);
    REQUIRE(file.is_open());
    file << "host=127.0.0.1\n";
    file << "reconnect_period_ms=\n";
  }
  CHECK_THROWS_AS(load_test_client_profile_from_file(missing_value_path),
                  std::invalid_argument);

  const std::string invalid_line_path =
      test_profile_ini_path("test-client-profile-invalid-line.ini");
  {
    std::ofstream file(invalid_line_path, std::ios::trunc);
    REQUIRE(file.is_open());
    file << "host=127.0.0.1\n";
    file << "broken-line-without-equals\n";
  }
  CHECK_THROWS_AS(load_test_client_profile_from_file(invalid_line_path),
                  std::invalid_argument);

  const std::string empty_key_path =
      test_profile_ini_path("test-client-profile-empty-key.ini");
  {
    std::ofstream file(empty_key_path, std::ios::trunc);
    REQUIRE(file.is_open());
    file << "=value\n";
  }
  CHECK_THROWS_AS(load_test_client_profile_from_file(empty_key_path),
                  std::invalid_argument);
}

TEST_CASE("test_client_profile_transport_helpers_cover_all_variants",
          "[test_client][profile]") {
  CHECK(to_string(TestClientTransport::Mqtt) == "mqtt");
  CHECK(to_string(TestClientTransport::Ws) == "ws");

  const auto mqtt_transport = transport_from_string("mqtt");
  REQUIRE(mqtt_transport.has_value());
  CHECK(*mqtt_transport == TestClientTransport::Mqtt);

  const auto ws_transport = transport_from_string("ws");
  REQUIRE(ws_transport.has_value());
  CHECK(*ws_transport == TestClientTransport::Ws);

  CHECK_FALSE(transport_from_string("invalid").has_value());
}

TEST_CASE("test_client_profile_step28_step29_roundtrip_preserves_extended_fields",
          "[test_client][profile]") {
  TestClientProfile profile;
  profile.host = "127.0.0.1";
  profile.port = 1883U;
  profile.client_id = "step29-client";
  profile.session_expiry_interval_seconds = 111U;
  profile.receive_maximum = 222U;
  profile.maximum_packet_size = 4096U;
  profile.topic_alias_maximum = 12U;
  profile.request_response_information = true;
  profile.request_problem_information = false;
  profile.connect_user_properties.emplace_back("k1", "v1");
  profile.authentication_method = std::string{"SCRAM-SHA-256"};
  profile.authentication_data = std::string{"token"};

  profile.will_topic = std::string{"will/topic"};
  profile.will_payload = "will-payload";
  profile.will_qos = 2U;
  profile.will_retain = true;
  profile.will_delay_interval_seconds = 45U;
  profile.will_payload_format_indicator = static_cast<uint8_t>(1U);
  profile.will_message_expiry_interval_seconds = 300U;
  profile.will_content_type = std::string{"application/json"};
  profile.will_response_topic = std::string{"response/topic"};
  profile.will_correlation_data = std::string{"0102"};
  profile.will_user_properties.emplace_back("wk", "wv");

  profile.publish_topic = std::string{"publish/topic"};
  profile.publish_qos = 1U;
  profile.publish_retain = true;
  profile.publish_dup = true;
  profile.publish_payload = std::string{"48656c6c6f"};
  profile.publish_payload_encoding = "hex";
  profile.publish_payload_format_indicator = static_cast<uint8_t>(1U);
  profile.publish_message_expiry_interval_seconds = 60U;
  profile.publish_topic_alias = 7U;
  profile.publish_response_topic = std::string{"reply/topic"};
  profile.publish_correlation_data = std::string{"dGVzdA=="};
  profile.publish_correlation_data_encoding = "base64";
  profile.publish_subscription_identifier = 9U;
  profile.publish_content_type = std::string{"text/plain"};
  profile.publish_user_properties.emplace_back("pk", "pv");
  profile.subscribe_entries.push_back("sensor/+/temp|1|false|false|0");
  profile.subscribe_identifier = 17U;
  profile.subscribe_user_properties.emplace_back("sk", "sv");
  profile.subscribe_clean_output = true;
  profile.subscribe_verbose_packets = true;
  profile.subscribe_output_file = std::string{"subscribe.out"};
  profile.subscribe_output_append = true;
  profile.subscribe_output_delimiter = "\\n---\\n";
  profile.subscribe_output_format = std::string{"{topic}:{payload}"};
  profile.subscribe_message_limit = 3U;
  profile.subscribe_wait_timeout_ms = 2500U;

  const std::string path =
      test_profile_ini_path("test-client-profile-step29-roundtrip.ini");
  save_test_client_profile_to_file(path, profile);

  const TestClientProfile loaded_profile =
      load_test_client_profile_from_file(path);

  CHECK(loaded_profile.session_expiry_interval_seconds == 111U);
  CHECK(loaded_profile.receive_maximum == 222U);
  CHECK(loaded_profile.maximum_packet_size == 4096U);
  CHECK(loaded_profile.topic_alias_maximum == 12U);
  CHECK(loaded_profile.request_response_information);
  CHECK_FALSE(loaded_profile.request_problem_information);
  REQUIRE(loaded_profile.connect_user_properties.size() == 1U);
  CHECK(loaded_profile.connect_user_properties[0].first == "k1");
  CHECK(loaded_profile.connect_user_properties[0].second == "v1");
  REQUIRE(loaded_profile.authentication_method.has_value());
  CHECK(*loaded_profile.authentication_method == "SCRAM-SHA-256");
  REQUIRE(loaded_profile.authentication_data.has_value());
  CHECK(*loaded_profile.authentication_data == "token");

  REQUIRE(loaded_profile.will_topic.has_value());
  CHECK(*loaded_profile.will_topic == "will/topic");
  CHECK(loaded_profile.will_qos == 2U);
  CHECK(loaded_profile.will_retain);
  CHECK(loaded_profile.will_delay_interval_seconds == 45U);
  REQUIRE(loaded_profile.will_payload_format_indicator.has_value());
  CHECK(*loaded_profile.will_payload_format_indicator == 1U);
  REQUIRE(loaded_profile.will_message_expiry_interval_seconds.has_value());
  CHECK(*loaded_profile.will_message_expiry_interval_seconds == 300U);
  REQUIRE(loaded_profile.will_content_type.has_value());
  CHECK(*loaded_profile.will_content_type == "application/json");
  REQUIRE(loaded_profile.will_response_topic.has_value());
  CHECK(*loaded_profile.will_response_topic == "response/topic");
  REQUIRE(loaded_profile.will_correlation_data.has_value());
  CHECK(*loaded_profile.will_correlation_data == "0102");
  REQUIRE(loaded_profile.will_user_properties.size() == 1U);
  CHECK(loaded_profile.will_user_properties[0].first == "wk");
  CHECK(loaded_profile.will_user_properties[0].second == "wv");

  REQUIRE(loaded_profile.publish_topic.has_value());
  CHECK(*loaded_profile.publish_topic == "publish/topic");
  CHECK(loaded_profile.publish_qos == 1U);
  CHECK(loaded_profile.publish_retain);
  CHECK(loaded_profile.publish_dup);
  REQUIRE(loaded_profile.publish_payload.has_value());
  CHECK(*loaded_profile.publish_payload == "48656c6c6f");
  CHECK(loaded_profile.publish_payload_encoding == "hex");
  REQUIRE(loaded_profile.publish_payload_format_indicator.has_value());
  CHECK(*loaded_profile.publish_payload_format_indicator == 1U);
  REQUIRE(loaded_profile.publish_message_expiry_interval_seconds.has_value());
  CHECK(*loaded_profile.publish_message_expiry_interval_seconds == 60U);
  REQUIRE(loaded_profile.publish_topic_alias.has_value());
  CHECK(*loaded_profile.publish_topic_alias == 7U);
  REQUIRE(loaded_profile.publish_response_topic.has_value());
  CHECK(*loaded_profile.publish_response_topic == "reply/topic");
  REQUIRE(loaded_profile.publish_correlation_data.has_value());
  CHECK(*loaded_profile.publish_correlation_data == "dGVzdA==");
  CHECK(loaded_profile.publish_correlation_data_encoding == "base64");
  REQUIRE(loaded_profile.publish_subscription_identifier.has_value());
  CHECK(*loaded_profile.publish_subscription_identifier == 9U);
  REQUIRE(loaded_profile.publish_content_type.has_value());
  CHECK(*loaded_profile.publish_content_type == "text/plain");
  REQUIRE(loaded_profile.publish_user_properties.size() == 1U);
  CHECK(loaded_profile.publish_user_properties[0].first == "pk");
  CHECK(loaded_profile.publish_user_properties[0].second == "pv");

  REQUIRE(loaded_profile.subscribe_entries.size() == 1U);
  CHECK(loaded_profile.subscribe_entries[0] == "sensor/+/temp|1|false|false|0");
  REQUIRE(loaded_profile.subscribe_identifier.has_value());
  CHECK(*loaded_profile.subscribe_identifier == 17U);
  REQUIRE(loaded_profile.subscribe_user_properties.size() == 1U);
  CHECK(loaded_profile.subscribe_user_properties[0].first == "sk");
  CHECK(loaded_profile.subscribe_user_properties[0].second == "sv");
  CHECK(loaded_profile.subscribe_clean_output);
  CHECK(loaded_profile.subscribe_verbose_packets);
  REQUIRE(loaded_profile.subscribe_output_file.has_value());
  CHECK(*loaded_profile.subscribe_output_file == "subscribe.out");
  CHECK(loaded_profile.subscribe_output_append);
  CHECK(loaded_profile.subscribe_output_delimiter == "\\n---\\n");
  REQUIRE(loaded_profile.subscribe_output_format.has_value());
  CHECK(*loaded_profile.subscribe_output_format == "{topic}:{payload}");
  CHECK(loaded_profile.subscribe_message_limit == 3U);
  CHECK(loaded_profile.subscribe_wait_timeout_ms == 2500U);
}

TEST_CASE("test_client_profile_step28_step29_validation_rejects_invalid_combinations",
          "[test_client][profile]") {
  TestClientProfile profile;

  profile.maximum_packet_size = 10U;
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.receive_maximum = 0U;
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.authentication_data = std::string{"token"};
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.will_payload = "payload";
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.will_topic = std::string{"topic"};
  profile.will_qos = 3U;
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.publish_payload = std::string{"test"};
  profile.publish_payload_stdin = true;
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.publish_payload_encoding = "unsupported";
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.publish_correlation_data_encoding = "unsupported";
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.publish_qos = 0U;
  profile.publish_dup = true;
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.publish_payload_format_indicator = static_cast<uint8_t>(2U);
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.publish_topic_alias = 0U;
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.publish_subscription_identifier = 0U;
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.subscribe_entries.push_back("topic-only");
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.subscribe_entries.push_back("sensor/1|3|false|false|0");
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.subscribe_output_append = true;
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);

  profile = TestClientProfile{};
  profile.subscribe_identifier = 0U;
  CHECK_THROWS_AS(validate_test_client_profile_or_throw(profile),
                  std::invalid_argument);
}

TEST_CASE("test_client_cli_connect_parses_profile_and_overrides",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient",      "connect",          "--profile",
                        "profile.ini",         "--transport",      "ws",
                        "--host",              "broker.example",   "--ws-path",
                        "/mqtt",               "--reconnect-period-ms",
                        "2000"};

  const TestClientCliOptions options = parse_test_client_cli(12, argv);
  CHECK(options.command == TestClientCommand::Connect);
  CHECK(options.profile_path == "profile.ini");
  REQUIRE(options.overrides.size() == 4U);
  CHECK(options.overrides[0].first == "transport");
  CHECK(options.overrides[0].second == "ws");
  CHECK(options.overrides[1].first == "host");
  CHECK(options.overrides[1].second == "broker.example");
  CHECK(options.overrides[2].first == "ws_path");
  CHECK(options.overrides[2].second == "/mqtt");
  CHECK(options.overrides[3].first == "reconnect_period_ms");
  CHECK(options.overrides[3].second == "2000");
}

TEST_CASE("test_client_cli_save_profile_requires_output",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "save-profile", "--host",
                        "127.0.0.1"};

  CHECK_THROWS_AS(parse_test_client_cli(4, argv), std::invalid_argument);
}

TEST_CASE("test_client_cli_show_profile_parses_basic_options",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "show-profile", "--profile",
                        "saved.ini",      "--client-id",  "shell-client"};

  const TestClientCliOptions options = parse_test_client_cli(6, argv);
  CHECK(options.command == TestClientCommand::ShowProfile);
  CHECK(options.profile_path == "saved.ini");
  REQUIRE(options.overrides.size() == 1U);
  CHECK(options.overrides[0].first == "client_id");
  CHECK(options.overrides[0].second == "shell-client");
}

TEST_CASE("test_client_cli_rejects_unknown_option", "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "connect", "--unknown", "value"};

  CHECK_THROWS_AS(parse_test_client_cli(4, argv), std::invalid_argument);
}

TEST_CASE("test_client_cli_save_profile_with_all_supported_options_succeeds",
          "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient",
      "save-profile",
      "--output",
      "saved.ini",
      "--profile",
      "base.ini",
      "--host",
      "127.0.0.1",
      "--port",
      "1883",
      "--transport",
      "ws",
      "--ws-path",
      "/mqtt",
      "--ws-header",
      "X-Test: one",
      "--client-id",
      "client-a",
      "--clean-start",
      "true",
      "--keep-alive-seconds",
      "20",
      "--username",
      "user",
      "--password",
      "secret",
      "--reconnect-period-ms",
      "500",
      "--maximum-reconnect-times",
      "5",
  };

  const TestClientCliOptions options = parse_test_client_cli(30, argv);
  CHECK(options.command == TestClientCommand::SaveProfile);
  CHECK(options.output_path == "saved.ini");
  CHECK(options.profile_path == "base.ini");
  CHECK(options.overrides.size() == 12U);
}

TEST_CASE("test_client_cli_help_and_error_paths", "[test_client][cli]") {
  {
    const char *argv[] = {"yahatestclient"};
    const TestClientCliOptions options = parse_test_client_cli(1, argv);
    CHECK(options.command == TestClientCommand::Help);
  }

  {
    const char *argv[] = {"yahatestclient", "--help"};
    const TestClientCliOptions options = parse_test_client_cli(2, argv);
    CHECK(options.command == TestClientCommand::Help);
  }

  {
    const char *argv[] = {"yahatestclient", "invalid-command"};
    CHECK_THROWS_AS(parse_test_client_cli(2, argv), std::invalid_argument);
  }

  {
    const char *argv[] = {"yahatestclient", "connect", "--host"};
    CHECK_THROWS_AS(parse_test_client_cli(3, argv), std::invalid_argument);
  }

  const std::string help_text = test_client_help_text();
  CHECK(help_text.find("yahatestclient") != std::string::npos);
  CHECK(help_text.find("save-profile") != std::string::npos);
  CHECK(help_text.find("publish") != std::string::npos);
  CHECK(help_text.find("conn") != std::string::npos);
  CHECK(help_text.find("simulate") != std::string::npos);
  CHECK(help_text.find("--version") != std::string::npos);

  const std::string version_text = test_client_version_text();
  CHECK(version_text.find("yahatestclient ") == 0U);
}

TEST_CASE("test_client_cli_publish_parses_step29_options", "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient",
      "publish",
      "--topic",
      "topic/a",
      "--qos",
      "2",
      "--retain",
      "true",
      "--dup",
      "true",
      "--payload",
      "48656c6c6f",
      "--payload-encoding",
      "hex",
      "--payload-format-indicator",
      "1",
      "--message-expiry-interval-seconds",
      "20",
      "--topic-alias",
      "3",
      "--response-topic",
      "reply/a",
      "--correlation-data",
      "dGVzdA==",
      "--correlation-data-encoding",
      "base64",
      "--subscription-identifier",
      "7",
      "--content-type",
      "text/plain",
      "--publish-user-property",
      "name=value",
  };

  const TestClientCliOptions options = parse_test_client_cli(32, argv);
  CHECK(options.command == TestClientCommand::Publish);
  REQUIRE(options.overrides.size() == 15U);
  CHECK(options.overrides[0].first == "publish_topic");
  CHECK(options.overrides[0].second == "topic/a");
  CHECK(options.overrides[14].first == "publish_user_property");
  CHECK(options.overrides[14].second == "name=value");
}

TEST_CASE("test_client_cli_connect_parses_step28_options", "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient",
      "connect",
      "--session-expiry-interval-seconds",
      "30",
      "--receive-maximum",
      "50",
      "--maximum-packet-size",
      "1000",
      "--topic-alias-maximum",
      "9",
      "--request-response-information",
      "true",
      "--request-problem-information",
      "false",
      "--connect-user-property",
      "k=v",
      "--authentication-method",
      "SCRAM",
      "--authentication-data",
      "abc",
      "--will-topic",
      "will/t",
      "--will-payload",
      "bye",
      "--will-qos",
      "1",
      "--will-retain",
      "true",
      "--will-delay-interval-seconds",
      "5",
      "--will-payload-format-indicator",
      "1",
      "--will-message-expiry-interval-seconds",
      "25",
      "--will-content-type",
      "application/json",
      "--will-response-topic",
      "resp/t",
      "--will-correlation-data",
      "0102",
      "--will-user-property",
      "wk=wv",
  };

  const TestClientCliOptions options = parse_test_client_cli(42, argv);
  CHECK(options.command == TestClientCommand::Connect);
  REQUIRE(options.overrides.size() == 20U);
  CHECK(options.overrides[0].first == "session_expiry_interval_seconds");
  CHECK(options.overrides[0].second == "30");
  CHECK(options.overrides[19].first == "will_user_property");
  CHECK(options.overrides[19].second == "wk=wv");
}

TEST_CASE("test_client_cli_subscribe_parses_step30_options", "[test_client][cli]") {
  const char *argv[] = {
      "yahatestclient",
      "subscribe",
      "--subscription",
      "sensor/+/temp|1|false|true|2",
      "--subscribe-identifier",
      "42",
      "--subscribe-user-property",
      "name=value",
      "--clean-output",
      "--verbose-packets",
      "--output-file",
      "sub.log",
      "--append-output",
      "--output-delimiter",
      "\\\\n",
      "--output-format",
      "{topic}:{payload}",
      "--message-limit",
      "3",
      "--wait-timeout-ms",
      "1500",
  };

  const TestClientCliOptions options = parse_test_client_cli(21, argv);
  CHECK(options.command == TestClientCommand::Subscribe);
  REQUIRE(options.overrides.size() == 11U);
  CHECK(options.overrides[0].first == "subscribe_entry");
  CHECK(options.overrides[0].second == "sensor/+/temp|1|false|true|2");
  CHECK(options.overrides[10].first == "subscribe_wait_timeout_ms");
  CHECK(options.overrides[10].second == "1500");
}

TEST_CASE("test_client_cli_scenario_parses_step31_options",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "scenario", "--scenario",
                        "qos1_subscribe_publish_unsubscribe", "--host",
                        "127.0.0.1"};

  const TestClientCliOptions options = parse_test_client_cli(6, argv);
  CHECK(options.command == TestClientCommand::Scenario);
  CHECK(options.scenario_name == "qos1_subscribe_publish_unsubscribe");
  CHECK_FALSE(options.list_scenarios);
}

TEST_CASE("test_client_cli_scenario_parses_step32_load_options",
          "[test_client][cli]") {
  const char *argv[] = {"yahatestclient",      "scenario",
                        "--load-mode",         "publish-rate",
                        "--connection-count",  "12",
                        "--connect-interval-ms", "20",
                        "--message-interval-ms", "5",
                        "--publish-limit",     "200",
                        "--parallelism",       "24",
                        "--topic-template",    "bench/{index}",
                        "--client-template",   "bench-client-{index}",
                        "--metrics-json"};

  const TestClientCliOptions options = parse_test_client_cli(19, argv);
  CHECK(options.command == TestClientCommand::Scenario);
  CHECK(options.load_mode == "publish-rate");
  CHECK(options.load_connection_count == 12U);
  CHECK(options.load_connect_interval_ms == 20U);
  CHECK(options.load_message_interval_ms == 5U);
  CHECK(options.load_publish_limit == 200U);
  CHECK(options.load_parallelism == 24U);
  CHECK(options.load_topic_template == "bench/{index}");
  CHECK(options.load_client_template == "bench-client-{index}");
  CHECK(options.load_metrics_json);
}

TEST_CASE("test_client_cli_scenario_requires_selector", "[test_client][cli]") {
  const char *argv[] = {"yahatestclient", "scenario"};
  CHECK_THROWS_AS(parse_test_client_cli(2, argv), std::invalid_argument);
}

} // namespace mqtt
