#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <csignal>
#include <filesystem>

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "broker/broker_error.h"
#include "connection/topic_alias_table.h"
#include "data_model/message/message.h"
#include "data_model/subscription/subscription.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"
#include "will_manager/will_publisher.h"

using namespace mqtt;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers

namespace {

/// Build a BrokerConfig that binds a single MQTT TCP listener on a
/// high-numbered test port unlikely to conflict in CI environments.
BrokerConfig make_test_config() {
  BrokerConfig cfg;
  cfg.mqtt_port = 18883U; // dedicated test port
  cfg.ws_port = 0U;       // disabled
  cfg.allow_anonymous = true;
  cfg.persistence_enabled = false;
  return cfg;
}

std::filesystem::path make_temp_dir() {
  auto dir = std::filesystem::temp_directory_path() / "broker_test_data";
  std::filesystem::create_directories(dir);
  return dir;
}

void remove_temp_dir(const std::filesystem::path &dir) {
  std::filesystem::remove_all(dir);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Initial state

TEST_CASE("broker_initially_not_running", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  CHECK(broker.is_running() == false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle

TEST_CASE("broker_running_after_startup", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();
  CHECK(broker.is_running() == true);
  broker.shutdown();
}

TEST_CASE("broker_not_running_after_shutdown", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();
  broker.shutdown();
  CHECK(broker.is_running() == false);
}

TEST_CASE("broker_startup_already_running_throws", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();
  try {
    broker.startup();
    FAIL("Expected BrokerException");
  } catch (const BrokerException &exc) {
    CHECK(exc.error() == BrokerError::AlreadyRunning);
  }
  broker.shutdown();
}

TEST_CASE("broker_shutdown_idempotent", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();
  broker.shutdown();
  broker.shutdown(); // second shutdown is a no-op
  CHECK(broker.is_running() == false);
}

TEST_CASE("broker_destructor_auto_shutdown", "[broker]") {
  BrokerConfig cfg = make_test_config();
  {
    Broker broker(cfg);
    broker.startup();
    CHECK(broker.is_running() == true);
    // Destructor calls shutdown() -- no crash
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Module accessors

TEST_CASE("broker_module_accessors_after_startup", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  auto &sess_mgr = broker.session_manager();
  auto &msg_rtr = broker.message_router();
  auto &auth = broker.authenticator();
  auto &acl = broker.acl_engine();
  auto &will_pub = broker.will_publisher();

  (void)sess_mgr;
  (void)msg_rtr;
  (void)auth;
  (void)acl;
  (void)will_pub;

  broker.shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Connection registration

TEST_CASE("broker_register_unregister_connection", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  bool delivered = false;
  broker.register_connection("client1",
                             [&](const Message &) { delivered = true; });

  broker.unregister_connection("client1");
  broker.unregister_connection("client1"); // idempotent

  broker.shutdown();
  CHECK_FALSE(delivered);
}

// ─────────────────────────────────────────────────────────────────────────────
// Signal handling

TEST_CASE("broker_shutdown_requested_false_initially", "[broker]") {
  Broker::install_signal_handlers();
  CHECK(Broker::shutdown_requested() == false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Persistence

TEST_CASE("broker_with_persistence_startup", "[broker]") {
  const auto tmp_dir = make_temp_dir();
  BrokerConfig cfg = make_test_config();
  cfg.persistence_enabled = true;
  cfg.persistence_dir = tmp_dir;

  {
    Broker broker(cfg);
    broker.startup();
    CHECK(broker.is_running() == true);
    broker.shutdown();
  }

  remove_temp_dir(tmp_dir);
}

// ─────────────────────────────────────────────────────────────────────────────
// Auth — password mode

TEST_CASE("broker_password_auth_when_not_anonymous", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  Broker broker(cfg);
  broker.startup();
  CHECK(broker.is_running() == true);
  broker.shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Signal handling — handle_signal path

TEST_CASE("broker_handle_signal_sets_shutdown_requested", "[broker]") {
  // Install handlers so the C signal handler is pointing to
  // Broker::handle_signal.
  Broker::install_signal_handlers();
  CHECK(Broker::shutdown_requested() == false);

  // std::raise delivers the signal synchronously; the handler runs inline.
  std::raise(SIGINT);
  CHECK(Broker::shutdown_requested() == true);

  // Reset for subsequent tests.
  Broker::install_signal_handlers();
  CHECK(Broker::shutdown_requested() == false);
}

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket listener

TEST_CASE("broker_ws_listener_startup_and_shutdown", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.ws_port = 18884U; // dedicated WS test port
  Broker broker(cfg);
  broker.startup();
  CHECK(broker.is_running() == true);
  broker.shutdown();
  CHECK(broker.is_running() == false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Online delivery — is_online + deliver lambdas

TEST_CASE("broker_message_delivered_to_online_connection", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  // Subscribe "sub_client" to "chat/room".
  Subscription sub;
  sub.topic_filter = Utf8String{"chat/room"};
  sub.qos = QoS::AtMostOnce;
  broker.subscription_store().store("sub_client", sub);

  // Register sub_client as online.
  bool delivered = false;
  broker.register_connection("sub_client",
                             [&](const Message &) { delivered = true; });

  // Route a message; the allow-all anonymous ACL passes for any client.
  Message msg;
  msg.topic = Utf8String{"chat/room"};
  msg.qos = QoS::AtMostOnce;
  TopicAliasTable alias_table(0U);
  broker.message_router().route(msg, "pub_client", "", alias_table);

  CHECK(delivered == true);

  broker.unregister_connection("sub_client");
  broker.shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Will publish callback — will_publish_fn lambda

TEST_CASE("broker_will_publish_fn_routes_will_message", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  // Subscribe "will_sub" to "client/will".
  Subscription sub;
  sub.topic_filter = Utf8String{"client/will"};
  sub.qos = QoS::AtMostOnce;
  broker.subscription_store().store("will_sub", sub);

  // Register will_sub as online.
  bool will_delivered = false;
  broker.register_connection("will_sub",
                             [&](const Message &) { will_delivered = true; });

  // Store a will with delay_interval = 0 so it fires immediately on
  // connection loss (no need to call publish_due()).
  WillMessage will;
  will.message.topic = Utf8String{"client/will"};
  will.message.qos = QoS::AtMostOnce;
  will.delay_interval = 0U;
  broker.will_publisher().on_connect("will_fire_client", will);

  // Simulate abrupt connection loss — delay 0 → publishes immediately.
  broker.will_publisher().on_connection_lost("will_fire_client",
                                             std::chrono::steady_clock::now());

  CHECK(will_delivered == true);

  broker.unregister_connection("will_sub");
  broker.shutdown();
}