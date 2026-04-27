#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <stdexcept>
#include <string>

#include "test_client/test_client_cli.h"
#include "test_client/test_client_profile.h"

namespace mqtt {

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

  const std::string path = "test-client-profile-roundtrip.ini";
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
  const std::string path = "test-client-profile-invalid.ini";
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

  const std::string missing_value_path = "test-client-profile-missing-value.ini";
  {
    std::ofstream file(missing_value_path, std::ios::trunc);
    REQUIRE(file.is_open());
    file << "host=127.0.0.1\n";
    file << "reconnect_period_ms=\n";
  }
  CHECK_THROWS_AS(load_test_client_profile_from_file(missing_value_path),
                  std::invalid_argument);

  const std::string invalid_line_path = "test-client-profile-invalid-line.ini";
  {
    std::ofstream file(invalid_line_path, std::ios::trunc);
    REQUIRE(file.is_open());
    file << "host=127.0.0.1\n";
    file << "broken-line-without-equals\n";
  }
  CHECK_THROWS_AS(load_test_client_profile_from_file(invalid_line_path),
                  std::invalid_argument);

  const std::string empty_key_path = "test-client-profile-empty-key.ini";
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
}

} // namespace mqtt
