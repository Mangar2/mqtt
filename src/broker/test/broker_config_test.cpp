#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "broker/broker_config.h"
#include "broker/broker_error.h"
#include "broker/config_loader.h"

using namespace mqtt;

//
// Helpers

namespace {

std::filesystem::path write_temp_file(const std::string &content) {
  auto path =
      std::filesystem::temp_directory_path() / "broker_config_test_temp.ini";
  std::ofstream out(path, std::ios::trunc);
  out << content;
  return path;
}

} // namespace

//
// ConfigLoader::parse — defaults

TEST_CASE("parse_minimal_valid_config", "[broker]") {
  const auto cfg = ConfigLoader::parse("[network]\nmqtt_port = 1883\n");
  CHECK(cfg.mqtt_port == 1883U);
  CHECK(cfg.ws_port == 0U);
  CHECK(cfg.allow_anonymous == true);
  CHECK(cfg.max_connections == 1000U);
  CHECK(cfg.receive_maximum == 65535U);
  CHECK(cfg.server_keep_alive == 0U);
  CHECK(cfg.session_expiry_max == 0U);
  CHECK(cfg.topic_alias_maximum == 10U);
  CHECK(cfg.max_queued_messages == 100U);
    CHECK(cfg.write_queue_max_bytes ==
      BrokerConfig::k_write_queue_max_bytes_default);
  CHECK(cfg.qos_retransmit_timeout_seconds == 20U);
  CHECK(cfg.tick_interval_ms == 100U);
  CHECK(cfg.persistence_enabled == false);
  CHECK(cfg.trace_max_text_length ==
        BrokerConfig::k_trace_text_max_length_default);
}

TEST_CASE("parse_all_network_keys", "[broker]") {
  const auto cfg =
      ConfigLoader::parse("[network]\nmqtt_port = 1884\nws_port = 9001\n");
  CHECK(cfg.mqtt_port == 1884U);
  CHECK(cfg.ws_port == 9001U);
}

TEST_CASE("parse_broker_section", "[broker]") {
  const auto cfg = ConfigLoader::parse("[network]\nmqtt_port = 1883\n"
                                       "[broker]\n"
                                       "allow_anonymous = false\n"
                                       "max_connections = 500\n"
                                       "receive_maximum = 100\n"
                                       "server_keep_alive = 30\n"
                                       "session_expiry_max = 3600\n"
                                       "topic_alias_maximum = 20\n"
                                       "max_queued_messages = 50\n"
                                       "write_queue_max_bytes = 131072\n"
                                       "qos_retransmit_timeout_seconds = 35\n"
                                       "tick_interval_ms = 250\n");
  CHECK(cfg.allow_anonymous == false);
  CHECK(cfg.max_connections == 500U);
  CHECK(cfg.receive_maximum == 100U);
  CHECK(cfg.server_keep_alive == 30U);
  CHECK(cfg.session_expiry_max == 3600U);
  CHECK(cfg.topic_alias_maximum == 20U);
  CHECK(cfg.max_queued_messages == 50U);
  CHECK(cfg.write_queue_max_bytes == 131072U);
  CHECK(cfg.qos_retransmit_timeout_seconds == 35U);
  CHECK(cfg.tick_interval_ms == 250U);
}

TEST_CASE("parse_persistence_section", "[broker]") {
  const auto cfg =
      ConfigLoader::parse("[network]\nmqtt_port = 1883\n"
                          "[persistence]\nenabled = true\ndir = /tmp/data\n");
  CHECK(cfg.persistence_enabled == true);
  CHECK(cfg.persistence_dir == std::filesystem::path{"/tmp/data"});
}

TEST_CASE("parse_auth_credentials_section", "[broker]") {
  const auto cfg = ConfigLoader::parse("[network]\nmqtt_port = 1883\n"
                                       "[auth]\n"
                                       "credential = alice:s3cr3t\n"
                                       "credential = bob:pwd\n");
  REQUIRE(cfg.password_credentials.size() == 2U);
  CHECK(cfg.password_credentials.at(0U).username == "alice");
  CHECK(cfg.password_credentials.at(0U).password == "s3cr3t");
  CHECK(cfg.password_credentials.at(1U).username == "bob");
  CHECK(cfg.password_credentials.at(1U).password == "pwd");
}

TEST_CASE("parse_acl_rules_section", "[broker]") {
  const auto cfg = ConfigLoader::parse("[network]\nmqtt_port = 1883\n"
                                       "[acl]\n"
                                       "rule = deny,anonymous,publish,a/#\n"
                                       "rule = allow,dev1,subscribe,b/+\n");
  REQUIRE(cfg.acl_rules.size() == 2U);
  CHECK(cfg.acl_rules.at(0U).effect == "deny");
  CHECK(cfg.acl_rules.at(0U).principal == "anonymous");
  CHECK(cfg.acl_rules.at(0U).action == "publish");
  CHECK(cfg.acl_rules.at(0U).topic_pattern == "a/#");
  CHECK(cfg.acl_rules.at(1U).effect == "allow");
  CHECK(cfg.acl_rules.at(1U).principal == "dev1");
  CHECK(cfg.acl_rules.at(1U).action == "subscribe");
  CHECK(cfg.acl_rules.at(1U).topic_pattern == "b/+");
}

TEST_CASE("parse_acl_rule_invalid_format_throws", "[broker]") {
  CHECK_THROWS_AS(ConfigLoader::parse("[network]\nmqtt_port = 1883\n"
                                      "[acl]\n"
                                      "rule = deny,anonymous,publish\n"),
                  BrokerException);
}

TEST_CASE("parse_tracing_section_global_level_and_modules", "[broker]") {
  const auto cfg = ConfigLoader::parse("[network]\nmqtt_port = 1883\n"
                                       "[tracing]\n"
                                       "global_level = info\n"
                                       "trace_modules = broker, connection\n"
                                       "max_text_length = 4096\n");

  CHECK(cfg.trace_global_level == TraceLevel::Info);
  REQUIRE(cfg.trace_modules.size() == 2U);
  CHECK(cfg.trace_modules.at(0U) == "broker");
  CHECK(cfg.trace_modules.at(1U) == "connection");
  CHECK(cfg.trace_max_text_length == 4096U);
}

TEST_CASE("parse_tracing_invalid_level_throws", "[broker]") {
  CHECK_THROWS_AS(ConfigLoader::parse("[network]\nmqtt_port = 1883\n"
                                      "[tracing]\n"
                                      "global_level = verbose\n"),
                  BrokerException);
}

TEST_CASE("parse_tracing_max_text_length_zero_throws", "[broker]") {
  CHECK_THROWS_AS(ConfigLoader::parse("[network]\nmqtt_port = 1883\n"
                                      "[tracing]\n"
                                      "max_text_length = 0\n"),
                  BrokerException);
}

//
// ConfigLoader::parse — bool variants

TEST_CASE("parse_bool_true_variants", "[broker]") {
  for (const auto *val : {"true", "1", "yes"}) {
    const std::string text =
        std::string(
            "[network]\nmqtt_port = 1883\n[broker]\nallow_anonymous = ") +
        val + "\n";
    const auto cfg = ConfigLoader::parse(text);
    CHECK(cfg.allow_anonymous == true);
  }
}

TEST_CASE("parse_bool_false_variants", "[broker]") {
  for (const auto *val : {"false", "0", "no"}) {
    const std::string text =
        std::string(
            "[network]\nmqtt_port = 1883\n[broker]\nallow_anonymous = ") +
        val + "\n";
    const auto cfg = ConfigLoader::parse(text);
    CHECK(cfg.allow_anonymous == false);
  }
}

//
// ConfigLoader::parse — ignored elements

TEST_CASE("parse_ignores_comments", "[broker]") {
  const auto cfg =
      ConfigLoader::parse("# This is a comment\n[network]\nmqtt_port = 1883\n");
  CHECK(cfg.mqtt_port == 1883U);
}

TEST_CASE("parse_ignores_unknown_keys", "[broker]") {
  const auto cfg =
      ConfigLoader::parse("[network]\nunknown_key = 99\nmqtt_port = 1883\n");
  CHECK(cfg.mqtt_port == 1883U);
}

TEST_CASE("parse_trims_whitespace", "[broker]") {
  const auto cfg = ConfigLoader::parse("[network]\n  mqtt_port  =  1884  \n");
  CHECK(cfg.mqtt_port == 1884U);
}

//
// ConfigLoader::parse — error cases

TEST_CASE("parse_bool_invalid_throws", "[broker]") {
  CHECK_THROWS_AS(
      ConfigLoader::parse(
          "[network]\nmqtt_port = 1883\n[broker]\nallow_anonymous = maybe\n"),
      BrokerException);
}

TEST_CASE("parse_uint_negative_throws", "[broker]") {
  // Negative numbers are not valid — they contain a '-' character
  CHECK_THROWS_AS(
      ConfigLoader::parse(
          "[network]\nmqtt_port = 1883\n[broker]\nmax_connections = -1\n"),
      BrokerException);
}

TEST_CASE("parse_uint_overflow_throws", "[broker]") {
  CHECK_THROWS_AS(
      ConfigLoader::parse("[network]\nmqtt_port = "
                          "1883\n[broker]\nmax_connections = 4294967296\n"),
      BrokerException);
}

TEST_CASE("parse_uint16_overflow_throws", "[broker]") {
  CHECK_THROWS_AS(ConfigLoader::parse("[network]\nmqtt_port = 70000\n"),
                  BrokerException);
}

TEST_CASE("parse_server_keep_alive_uint16_overflow_throws", "[broker]") {
  CHECK_THROWS_AS(
      ConfigLoader::parse("[network]\nmqtt_port = 1883\n[broker]\n"
                          "server_keep_alive = 70000\n"),
      BrokerException);
}

TEST_CASE("parse_auth_credential_invalid_format_throws", "[broker]") {
  CHECK_THROWS_AS(
      ConfigLoader::parse("[network]\nmqtt_port = 1883\n"
                          "[auth]\n"
                          "credential = malformed_without_separator\n"),
      BrokerException);
}

TEST_CASE("parse_both_ports_zero_throws", "[broker]") {
  try {
    (void)ConfigLoader::parse("[network]\nmqtt_port = 0\nws_port = 0\n");
    FAIL("Expected BrokerException");
  } catch (const BrokerException &exc) {
    CHECK(exc.error() == BrokerError::NoListenerConfigured);
  }
}

TEST_CASE("parse_max_connections_zero_throws", "[broker]") {
  CHECK_THROWS_AS(
      ConfigLoader::parse(
          "[network]\nmqtt_port = 1883\n[broker]\nmax_connections = 0\n"),
      BrokerException);
}

TEST_CASE("parse_receive_maximum_zero_throws", "[broker]") {
  CHECK_THROWS_AS(
      ConfigLoader::parse(
          "[network]\nmqtt_port = 1883\n[broker]\nreceive_maximum = 0\n"),
      BrokerException);
}

TEST_CASE("parse_max_queued_zero_throws", "[broker]") {
  CHECK_THROWS_AS(
      ConfigLoader::parse(
          "[network]\nmqtt_port = 1883\n[broker]\nmax_queued_messages = 0\n"),
      BrokerException);
}

TEST_CASE("parse_write_queue_max_bytes_zero_throws", "[broker]") {
  CHECK_THROWS_AS(ConfigLoader::parse("[network]\nmqtt_port = 1883\n[broker]\n"
                                      "write_queue_max_bytes = 0\n"),
                  BrokerException);
}

TEST_CASE("parse_write_queue_max_bytes_over_hard_limit_throws", "[broker]") {
  const uint32_t invalid_value =
      BrokerConfig::k_write_queue_max_bytes_hard_limit + 1U;
  CHECK_THROWS_AS(
      ConfigLoader::parse("[network]\nmqtt_port = 1883\n[broker]\n"
                          "write_queue_max_bytes = " +
                          std::to_string(invalid_value) + "\n"),
      BrokerException);
}

TEST_CASE("parse_qos_retransmit_timeout_zero_throws", "[broker]") {
  CHECK_THROWS_AS(ConfigLoader::parse("[network]\nmqtt_port = 1883\n[broker]\n"
                                      "qos_retransmit_timeout_seconds = 0\n"),
                  BrokerException);
}

TEST_CASE("parse_tick_interval_zero_throws", "[broker]") {
  CHECK_THROWS_AS(ConfigLoader::parse("[network]\nmqtt_port = 1883\n[broker]\n"
                                      "tick_interval_ms = 0\n"),
                  BrokerException);
}

//
// ConfigLoader::load — file I/O

TEST_CASE("load_from_file", "[broker]") {
  const auto path = write_temp_file("[network]\nmqtt_port = 1884\n");
  const auto cfg = ConfigLoader::load(path);
  CHECK(cfg.mqtt_port == 1884U);
  std::filesystem::remove(path);
}

TEST_CASE("load_nonexistent_file_throws", "[broker]") {
  CHECK_THROWS_AS(ConfigLoader::load("/no/such/file_broker_test.conf"),
                  BrokerException);
}
