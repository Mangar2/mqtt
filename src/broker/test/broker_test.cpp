#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <optional>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "broker/broker_error.h"
#include "connection/topic_alias_table.h"
#include "data_model/message/message.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/property/property_id.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/session/inflight_entry.h"
#include "data_model/session/inflight_state.h"
#include "data_model/session/session_state.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"
#include "network/tcp_connection.h"
#include "persistence/inflight_persistence.h"
#include "persistence/retained_message_persistence.h"
#include "persistence/session_persistence.h"

using namespace mqtt;
using namespace std::chrono_literals;

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

void close_socket_handle(mqtt::SocketHandle socket_handle) {
#ifdef _WIN32
  ::closesocket(static_cast<SOCKET>(socket_handle));
#else
  ::close(static_cast<int>(socket_handle));
#endif
}

void connect_loopback(uint16_t port_value) {
  mqtt::SocketHandle socket_handle =
      ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(socket_handle != mqtt::k_invalid_socket);

  sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port_value);
  server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  const int connect_result =
      ::connect(socket_handle, reinterpret_cast<const sockaddr *>(&server_addr),
                sizeof(server_addr));
  CHECK(connect_result == 0);
  close_socket_handle(socket_handle);
}

std::optional<TwoByteInteger>
find_two_byte_property(const std::vector<Property> &properties,
                       PropertyId property_id) {
  for (const auto &property : properties) {
    if (property.id == property_id) {
      return std::get<TwoByteInteger>(property.value);
    }
  }
  return std::nullopt;
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

TEST_CASE("broker_persistence_startup_loads_seeded_records", "[broker]") {
  const auto tmp_dir = make_temp_dir();

  SessionState session;
  session.client_id = Utf8String{"seed_client"};
  session.session_expiry_interval = 60U;

  Message retained;
  retained.topic = Utf8String{"seed/retained"};
  retained.qos = QoS::AtMostOnce;
  retained.retain = true;

  InflightEntry inflight;
  inflight.packet_id = 1U;
  inflight.qos = QoS::AtLeastOnce;
  inflight.state = InflightState::WaitingForPuback;
  inflight.direction = InflightDirection::Outbound;
  inflight.message.topic = Utf8String{"seed/topic"};
  inflight.timestamp = std::chrono::steady_clock::now();

  SessionPersistence session_persistence(tmp_dir);
  RetainedMessagePersistence retained_persistence(tmp_dir);
  InflightPersistence inflight_persistence(tmp_dir);

  session_persistence.save_all(std::vector<SessionState>{session});
  retained_persistence.save_all(std::vector<Message>{retained});
  inflight_persistence.save_all(
      std::vector<InflightPersistence::ClientEntry>{{"seed_client", inflight}});

  BrokerConfig cfg = make_test_config();
  cfg.persistence_enabled = true;
  cfg.persistence_dir = tmp_dir;

  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"seed_client"};
  connect.clean_start = false;
  const ConnectResult result = broker.handle_connect(connect, []() {});
  CHECK(result.session_present == true);
  CHECK(result.reason_code == ReasonCode::Success);

  broker.shutdown();
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

TEST_CASE("broker_accept_loop_invokes_client_handler", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.mqtt_port = 18885U;

  Broker broker(cfg);
  broker.startup();

  connect_loopback(cfg.mqtt_port);
  std::this_thread::sleep_for(20ms);

  broker.shutdown();
  CHECK(broker.is_running() == false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Online delivery — is_online + deliver lambdas

TEST_CASE("broker_route_message_without_subscribers_is_safe", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  // Route a message; the allow-all anonymous ACL passes for any client.
  Message msg;
  msg.topic = Utf8String{"chat/room"};
  msg.qos = QoS::AtMostOnce;
  TopicAliasTable alias_table(0U);
  CHECK_NOTHROW(broker.route_message(msg, "pub_client", "", alias_table));
  CHECK(broker.statistics_collector().snapshot().messages_inbound == 1U);

  broker.shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Will publish callback — will_publish_fn lambda

TEST_CASE("broker_handle_connection_lost_unregisters_client", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  broker.register_connection("lost_client", [](const Message &) {});
  CHECK(broker.statistics_collector().snapshot().connected_clients == 1U);

  // Store a will and then simulate abrupt connection loss.
  WillMessage will;
  will.message.topic = Utf8String{"client/will"};
  will.message.qos = QoS::AtMostOnce;
  will.delay_interval = 0U;
  broker.will_publisher().on_connect("lost_client", will);

  broker.handle_connection_lost("lost_client",
                                std::chrono::steady_clock::now());
  CHECK(broker.statistics_collector().snapshot().connected_clients == 0U);

  broker.shutdown();
}

TEST_CASE("broker_handle_connect_returns_connect_result", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"conn_client"};
  connect.clean_start = false;

  const ConnectResult result = broker.handle_connect(connect, []() {});

  CHECK(result.session_present == false);
  CHECK(result.reason_code == ReasonCode::Success);
  CHECK(result.client_id == "conn_client");

  broker.shutdown();
}

TEST_CASE("broker_handle_connect_auth_failure_returns_reason", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"auth_fail_client"};

  const ConnectResult result = broker.handle_connect(connect, []() {});

  CHECK(result.reason_code == ReasonCode::BadUserNameOrPassword);
  CHECK(result.session_present == false);

  broker.shutdown();
}

TEST_CASE("broker_handle_connect_builds_connack_properties", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.receive_maximum = 123U;
  cfg.topic_alias_maximum = 77U;
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"connack_prop_client"};

  const ConnectResult result = broker.handle_connect(connect, []() {});

  CHECK(result.reason_code == ReasonCode::Success);
  const auto receive_maximum = find_two_byte_property(
      result.connack_properties, PropertyId::ReceiveMaximum);
  REQUIRE(receive_maximum.has_value());
  CHECK(*receive_maximum == 123U);

  const auto topic_alias_maximum = find_two_byte_property(
      result.connack_properties, PropertyId::TopicAliasMaximum);
  REQUIRE(topic_alias_maximum.has_value());
  CHECK(*topic_alias_maximum == 77U);

  broker.shutdown();
}

TEST_CASE("broker_handle_connect_invalid_client_id_returns_reason",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{""};

  const ConnectResult result = broker.handle_connect(connect, []() {});

  CHECK(result.reason_code == ReasonCode::ClientIdentifierNotValid);
  CHECK(result.session_present == false);

  broker.shutdown();
}

TEST_CASE("broker_handle_connect_with_will_properties_succeeds", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"will_client"};
  connect.clean_start = false;

  WillData will;
  will.topic = Utf8String{"device/will"};
  will.payload = BinaryData{{0x41U, 0x42U}};
  will.qos = QoS::AtLeastOnce;
  will.retain = true;
  will.properties.push_back(
      {PropertyId::WillDelayInterval, FourByteInteger{15U}});
  will.properties.push_back(
      {PropertyId::ContentType, Utf8String{"application/octet-stream"}});
  connect.will = will;

  const ConnectResult result = broker.handle_connect(connect, []() {});

  CHECK(result.reason_code == ReasonCode::Success);
  CHECK(result.client_id == "will_client");

  broker.shutdown();
}

TEST_CASE("broker_handle_disconnect_unregisters_client", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  broker.register_connection("disc_client", [](const Message &) {});
  CHECK(broker.statistics_collector().snapshot().connected_clients == 1U);

  broker.handle_disconnect("disc_client", ReasonCode::Success, std::nullopt,
                           std::chrono::steady_clock::now());
  CHECK(broker.statistics_collector().snapshot().connected_clients == 0U);

  broker.shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Monitoring — Module 16 integration

TEST_CASE("broker_statistics_collector_accessor", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  auto &stats = broker.statistics_collector();
  const auto snap = stats.snapshot();
  CHECK(snap.connected_clients == 0U);
  CHECK(snap.messages_inbound == 0U);
  CHECK(snap.messages_outbound == 0U);

  broker.shutdown();
}

TEST_CASE("broker_register_increments_connected_clients", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  broker.register_connection("c1", [](const Message &) {});
  CHECK(broker.statistics_collector().snapshot().connected_clients == 1U);

  broker.register_connection("c2", [](const Message &) {});
  CHECK(broker.statistics_collector().snapshot().connected_clients == 2U);

  broker.unregister_connection("c1");
  CHECK(broker.statistics_collector().snapshot().connected_clients == 1U);

  broker.unregister_connection("c2");
  CHECK(broker.statistics_collector().snapshot().connected_clients == 0U);

  broker.shutdown();
}

TEST_CASE("broker_register_same_client_does_not_double_count", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  broker.register_connection("same_client", [](const Message &) {});
  CHECK(broker.statistics_collector().snapshot().connected_clients == 1U);

  broker.register_connection("same_client", [](const Message &) {});
  CHECK(broker.statistics_collector().snapshot().connected_clients == 1U);

  broker.unregister_connection("same_client");
  CHECK(broker.statistics_collector().snapshot().connected_clients == 0U);

  broker.shutdown();
}

TEST_CASE("broker_route_message_counts_inbound", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  Message msg;
  msg.topic = Utf8String{"sensors/temp"};
  msg.qos = QoS::AtMostOnce;
  TopicAliasTable alias_table(0U);

  broker.route_message(msg, "pub_client", "", alias_table);
  CHECK(broker.statistics_collector().snapshot().messages_inbound == 1U);

  broker.route_message(msg, "pub_client", "", alias_table);
  CHECK(broker.statistics_collector().snapshot().messages_inbound == 2U);

  broker.shutdown();
}

TEST_CASE("broker_unregister_unknown_client_keeps_count", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  broker.unregister_connection("missing_client");
  CHECK(broker.statistics_collector().snapshot().connected_clients == 0U);

  broker.register_connection("present_client", [](const Message &) {});
  CHECK(broker.statistics_collector().snapshot().connected_clients == 1U);

  broker.unregister_connection("present_client");
  CHECK(broker.statistics_collector().snapshot().connected_clients == 0U);

  broker.unregister_connection("present_client");
  CHECK(broker.statistics_collector().snapshot().connected_clients == 0U);

  broker.shutdown();
}

TEST_CASE("broker_tick_returns_false_when_sys_disabled", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.sys_topic_interval = 0U; // disabled
  Broker broker(cfg);
  broker.startup();

  // Tick with a far-future time — should never publish when interval == 0.
  const auto far_future = std::chrono::steady_clock::now() + 1000s;
  CHECK_FALSE(broker.tick(far_future));

  broker.shutdown();
}

TEST_CASE("broker_tick_publishes_sys_topics_when_enabled", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.sys_topic_interval = 60U; // 60-second interval
  Broker broker(cfg);
  broker.startup();

  // First tick far in the future — interval always elapsed on first tick.
  const auto far_future = std::chrono::steady_clock::now() + 1000s;
  CHECK(broker.tick(far_future));

  broker.shutdown();
}