#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <optional>
#include <sstream>
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
#include "outbound_queue/outbound_queue.h"
#include "persistence/inflight_persistence.h"
#include "persistence/offline_queue_persistence.h"
#include "persistence/retained_message_persistence.h"
#include "persistence/session_persistence.h"

using namespace mqtt;
using namespace std::chrono_literals;

//
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

std::optional<uint8_t> find_byte_property(const std::vector<Property> &properties,
                                          PropertyId property_id) {
  for (const auto &property : properties) {
    if (property.id == property_id) {
      return std::get<uint8_t>(property.value);
    }
  }
  return std::nullopt;
}

std::optional<FourByteInteger>
find_four_byte_property(const std::vector<Property> &properties,
                        PropertyId property_id) {
  for (const auto &property : properties) {
    if (property.id == property_id) {
      return std::get<FourByteInteger>(property.value);
    }
  }
  return std::nullopt;
}

BinaryData binary_from_text(std::string_view text) {
  BinaryData binary;
  binary.data.reserve(text.size());
  for (char chr : text) {
    binary.data.push_back(static_cast<uint8_t>(chr));
  }
  return binary;
}

Property make_auth_method_property(std::string_view method_name) {
  return {PropertyId::AuthenticationMethod,
          Utf8String{std::string(method_name)}};
}

Property make_auth_data_property(std::string_view payload_text) {
  return {PropertyId::AuthenticationData, binary_from_text(payload_text)};
}

} // namespace

//
// Initial state

TEST_CASE("broker_initially_not_running", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  CHECK(broker.is_running() == false);
}

//
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

//
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

//
// Connection registration

TEST_CASE("broker_register_unregister_connection", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  auto queue = std::make_shared<OutboundQueue>();
  broker.register_connection("client1", queue);

  broker.unregister_connection("client1");
  broker.unregister_connection("client1"); // idempotent

  broker.shutdown();
  CHECK(queue->is_empty());
}

//
// Signal handling

TEST_CASE("broker_shutdown_requested_false_initially", "[broker]") {
  Broker::install_signal_handlers();
  CHECK(Broker::shutdown_requested() == false);
}

//
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

TEST_CASE("broker_persistence_startup_loads_seeded_offline_queue", "[broker]") {
  const auto tmp_dir = make_temp_dir();

  // Seed a session so persistence coordinator has something to load.
  SessionState session;
  session.client_id = Utf8String{"queue_client"};
  session.session_expiry_interval = 300U;

  SessionPersistence session_persistence(tmp_dir);
  session_persistence.save_all(std::vector<SessionState>{session});

  // Seed the offline queue with one message for queue_client.
  Message queued_msg;
  queued_msg.topic = Utf8String{"offline/topic"};
  queued_msg.qos = QoS::AtLeastOnce;

  OfflineQueuePersistence offline_persistence(tmp_dir);
  offline_persistence.save_all(
      std::vector<OfflineQueuePersistence::ClientMessages>{
          {"queue_client", {queued_msg}}});

  BrokerConfig cfg = make_test_config();
  cfg.persistence_enabled = true;
  cfg.persistence_dir = tmp_dir;

  Broker broker(cfg);
  broker.startup();
  CHECK(broker.is_running() == true);

  broker.shutdown();
  remove_temp_dir(tmp_dir);
}

TEST_CASE("broker_password_auth_when_not_anonymous", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  Broker broker(cfg);
  broker.startup();
  CHECK(broker.is_running() == true);
  broker.shutdown();
}

//
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

//
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

TEST_CASE("broker_reactor_accept_invokes_client_handler", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.mqtt_port = 18885U;

  Broker broker(cfg);
  broker.startup();

  connect_loopback(cfg.mqtt_port);
  std::this_thread::sleep_for(20ms);

  broker.shutdown();
  CHECK(broker.is_running() == false);
}

//
// Online delivery — is_online + deliver lambdas

TEST_CASE("broker_handle_publish_without_subscribers_is_safe", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  // Route a message; the allow-all anonymous ACL passes for any client.
  Message msg;
  msg.topic = Utf8String{"chat/room"};
  msg.qos = QoS::AtMostOnce;
  TopicAliasTable alias_table(0U);
  CHECK_NOTHROW(broker.handle_publish(msg, "pub_client", "", alias_table));
  CHECK(broker.statistics_collector().snapshot().messages_inbound == 1U);

  broker.shutdown();
}

//
// Will publish callback — will_publish_fn lambda

TEST_CASE("broker_handle_connection_lost_unregisters_client", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  broker.register_connection("lost_client", std::make_shared<OutboundQueue>());
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

TEST_CASE(
    "broker_handle_connect_password_auth_success_with_configured_credential",
    "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials.push_back(
      {.username = "alice", .password = "s3cr3t"});
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"auth_ok_client"};
  connect.username = Utf8String{"alice"};
  connect.password = binary_from_text("s3cr3t");

  const ConnectResult result = broker.handle_connect(connect, []() {});

  CHECK(result.reason_code == ReasonCode::Success);
  CHECK(result.auth_status == AuthStatus::Success);
  CHECK(result.session_present == false);

  broker.shutdown();
}

TEST_CASE("broker_handle_connect_enhanced_sets_auth_method", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"enhanced_client"};
  connect.properties.push_back(make_auth_method_property("PLAIN"));

  const ConnectResult result = broker.handle_connect(connect, []() {});

  CHECK(result.reason_code == ReasonCode::Success);
  CHECK(result.auth_status == AuthStatus::Success);
  CHECK(result.auth_method == "PLAIN");

  broker.shutdown();
}

TEST_CASE("broker_handle_connect_enhanced_continue_in_password_mode",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials.push_back(
      {.username = "alice", .password = "s3cr3t"});
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"enhanced_continue_client"};
  connect.properties.push_back(make_auth_method_property("PLAIN"));

  const ConnectResult result = broker.handle_connect(connect, []() {});

  CHECK(result.auth_status == AuthStatus::Continue);
  CHECK(result.reason_code == ReasonCode::ContinueAuthentication);
  CHECK(result.auth_method == "PLAIN");
  REQUIRE(result.auth_data.has_value());
  CHECK_FALSE(result.auth_data->data.empty());

  broker.shutdown();
}

TEST_CASE("broker_handle_connect_enhanced_bad_method_fails", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials.push_back(
      {.username = "alice", .password = "s3cr3t"});
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"enhanced_bad_method_client"};
  connect.properties.push_back(make_auth_method_property("SCRAM"));

  const ConnectResult result = broker.handle_connect(connect, []() {});

  CHECK(result.auth_status == AuthStatus::Failure);
  CHECK(result.reason_code == ReasonCode::BadAuthenticationMethod);

  broker.shutdown();
}

TEST_CASE("broker_handle_auth_packet_completes_pending_exchange_success",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials.push_back(
      {.username = "alice", .password = "s3cr3t"});
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"enhanced_pending_client"};
  connect.properties.push_back(make_auth_method_property("PLAIN"));
  const ConnectResult connect_result = broker.handle_connect(connect, []() {});
  REQUIRE(connect_result.auth_status == AuthStatus::Continue);

  AuthPacket auth_packet;
  auth_packet.reason_code = ReasonCode::ContinueAuthentication;
  auth_packet.properties.push_back(make_auth_method_property("PLAIN"));
  auth_packet.properties.push_back(make_auth_data_property("alice:s3cr3t"));

  const ConnectResult result =
      broker.handle_auth_packet("enhanced_pending_client", auth_packet);

  CHECK(result.auth_status == AuthStatus::Success);
  CHECK(result.reason_code == ReasonCode::Success);
  CHECK(result.client_id == "enhanced_pending_client");

  broker.shutdown();
}

TEST_CASE("broker_handle_auth_packet_failure_ends_pending_exchange",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials.push_back(
      {.username = "alice", .password = "s3cr3t"});
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"enhanced_pending_fail_client"};
  connect.properties.push_back(make_auth_method_property("PLAIN"));
  const ConnectResult connect_result = broker.handle_connect(connect, []() {});
  REQUIRE(connect_result.auth_status == AuthStatus::Continue);

  AuthPacket bad_packet;
  bad_packet.reason_code = ReasonCode::ContinueAuthentication;
  bad_packet.properties.push_back(make_auth_method_property("SCRAM"));
  bad_packet.properties.push_back(make_auth_data_property("alice:s3cr3t"));

  const ConnectResult failed =
      broker.handle_auth_packet("enhanced_pending_fail_client", bad_packet);
  CHECK(failed.auth_status == AuthStatus::Failure);
  CHECK(failed.reason_code == ReasonCode::BadAuthenticationMethod);

  const ConnectResult missing_after_failure =
      broker.handle_auth_packet("enhanced_pending_fail_client", bad_packet);
  CHECK(missing_after_failure.auth_status == AuthStatus::Failure);
  CHECK(missing_after_failure.reason_code == ReasonCode::ProtocolError);

  broker.shutdown();
}

TEST_CASE("broker_handle_auth_packet_missing_data_returns_continue",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials.push_back(
      {.username = "alice", .password = "s3cr3t"});
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"enhanced_pending_continue_client"};
  connect.properties.push_back(make_auth_method_property("PLAIN"));
  const ConnectResult connect_result = broker.handle_connect(connect, []() {});
  REQUIRE(connect_result.auth_status == AuthStatus::Continue);

  AuthPacket auth_packet;
  auth_packet.reason_code = ReasonCode::ContinueAuthentication;
  auth_packet.properties.push_back(make_auth_method_property("PLAIN"));

  const ConnectResult result = broker.handle_auth_packet(
      "enhanced_pending_continue_client", auth_packet);
  CHECK(result.auth_status == AuthStatus::Continue);
  CHECK(result.reason_code == ReasonCode::ContinueAuthentication);
  REQUIRE(result.auth_data.has_value());

  broker.shutdown();
}

TEST_CASE("broker_handle_auth_packet_malformed_data_returns_failure",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials.push_back(
      {.username = "alice", .password = "s3cr3t"});
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"enhanced_pending_malformed_client"};
  connect.properties.push_back(make_auth_method_property("PLAIN"));
  const ConnectResult connect_result = broker.handle_connect(connect, []() {});
  REQUIRE(connect_result.auth_status == AuthStatus::Continue);

  AuthPacket auth_packet;
  auth_packet.reason_code = ReasonCode::ContinueAuthentication;
  auth_packet.properties.push_back(make_auth_method_property("PLAIN"));
  auth_packet.properties.push_back(
      make_auth_data_property("malformed_payload"));

  const ConnectResult result = broker.handle_auth_packet(
      "enhanced_pending_malformed_client", auth_packet);
  CHECK(result.auth_status == AuthStatus::Failure);
  CHECK(result.reason_code == ReasonCode::BadUserNameOrPassword);

  broker.shutdown();
}

TEST_CASE("broker_handle_auth_packet_without_pending_exchange_protocol_error",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  AuthPacket auth_packet;
  auth_packet.reason_code = ReasonCode::ContinueAuthentication;
  auth_packet.properties.push_back(make_auth_method_property("PLAIN"));

  const ConnectResult result =
      broker.handle_auth_packet("missing_client", auth_packet);

  CHECK(result.auth_status == AuthStatus::Failure);
  CHECK(result.reason_code == ReasonCode::ProtocolError);

  broker.shutdown();
}

TEST_CASE("broker_handle_reauthenticate_success_for_enhanced_session",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"reauth_client"};
  connect.properties.push_back(make_auth_method_property("PLAIN"));
  const ConnectResult connect_result = broker.handle_connect(connect, []() {});
  REQUIRE(connect_result.reason_code == ReasonCode::Success);

  AuthPacket reauth_packet;
  reauth_packet.reason_code = ReasonCode::ReAuthenticate;
  reauth_packet.properties.push_back(make_auth_method_property("PLAIN"));

  const AuthResult reauth_result =
      broker.handle_reauthenticate("reauth_client", reauth_packet);

  CHECK(reauth_result.status == AuthStatus::Success);
  CHECK(reauth_result.reason_code == ReasonCode::Success);

  broker.shutdown();
}

TEST_CASE("broker_handle_reauthenticate_bad_method_returns_reason",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"reauth_bad_method_client"};
  connect.properties.push_back(make_auth_method_property("PLAIN"));
  const ConnectResult connect_result = broker.handle_connect(connect, []() {});
  REQUIRE(connect_result.reason_code == ReasonCode::Success);

  AuthPacket reauth_packet;
  reauth_packet.reason_code = ReasonCode::ReAuthenticate;
  reauth_packet.properties.push_back(make_auth_method_property("SCRAM"));

  const AuthResult reauth_result =
      broker.handle_reauthenticate("reauth_bad_method_client", reauth_packet);

  CHECK(reauth_result.status == AuthStatus::Failure);
  CHECK(reauth_result.reason_code == ReasonCode::BadAuthenticationMethod);

  broker.shutdown();
}

TEST_CASE(
    "broker_handle_reauthenticate_without_enhanced_session_protocol_error",
    "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  AuthPacket reauth_packet;
  reauth_packet.reason_code = ReasonCode::ReAuthenticate;
  reauth_packet.properties.push_back(make_auth_method_property("PLAIN"));

  const AuthResult reauth_result =
      broker.handle_reauthenticate("missing_enhanced_client", reauth_packet);

  CHECK(reauth_result.status == AuthStatus::Failure);
  CHECK(reauth_result.reason_code == ReasonCode::ProtocolError);

  broker.shutdown();
}

TEST_CASE("broker_handle_reauthenticate_bad_credentials_returns_failure",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials.push_back(
      {.username = "alice", .password = "s3cr3t"});
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"reauth_failure_client"};
  connect.properties.push_back(make_auth_method_property("PLAIN"));
  connect.properties.push_back(make_auth_data_property("alice:s3cr3t"));
  const ConnectResult connect_result = broker.handle_connect(connect, []() {});
  REQUIRE(connect_result.reason_code == ReasonCode::Success);

  AuthPacket reauth_packet;
  reauth_packet.reason_code = ReasonCode::ReAuthenticate;
  reauth_packet.properties.push_back(make_auth_method_property("PLAIN"));
  reauth_packet.properties.push_back(make_auth_data_property("alice:wrong"));

  const AuthResult reauth_result =
      broker.handle_reauthenticate("reauth_failure_client", reauth_packet);

  CHECK(reauth_result.status == AuthStatus::Failure);
  CHECK(reauth_result.reason_code == ReasonCode::BadUserNameOrPassword);

  broker.shutdown();
}

TEST_CASE("broker_handle_connect_builds_connack_properties", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.receive_maximum = 123U;
  cfg.server_keep_alive = 9U;
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

    const auto server_keep_alive = find_two_byte_property(
      result.connack_properties, PropertyId::ServerKeepAlive);
    REQUIRE(server_keep_alive.has_value());
    CHECK(*server_keep_alive == 9U);

    const auto maximum_qos =
      find_byte_property(result.connack_properties, PropertyId::MaximumQoS);
    REQUIRE(maximum_qos.has_value());
    CHECK(*maximum_qos == 2U);

    const auto retain_available =
      find_byte_property(result.connack_properties, PropertyId::RetainAvailable);
    REQUIRE(retain_available.has_value());
    CHECK(*retain_available == 1U);

    const auto maximum_packet_size = find_four_byte_property(
      result.connack_properties, PropertyId::MaximumPacketSize);
    REQUIRE(maximum_packet_size.has_value());
    CHECK(*maximum_packet_size == 0x0FFFFFFFU);

    const auto wildcard_subscription_available = find_byte_property(
      result.connack_properties, PropertyId::WildcardSubscriptionAvailable);
    REQUIRE(wildcard_subscription_available.has_value());
    CHECK(*wildcard_subscription_available == 1U);

    const auto subscription_identifier_available = find_byte_property(
      result.connack_properties, PropertyId::SubscriptionIdentifierAvailable);
    REQUIRE(subscription_identifier_available.has_value());
    CHECK(*subscription_identifier_available == 1U);

    const auto shared_subscription_available = find_byte_property(
      result.connack_properties, PropertyId::SharedSubscriptionAvailable);
    REQUIRE(shared_subscription_available.has_value());
    CHECK(*shared_subscription_available == 1U);

  broker.shutdown();
}

TEST_CASE("broker_handle_connect_omits_server_keep_alive_when_disabled",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.server_keep_alive = 0U;

  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"connack_no_server_keep_alive_client"};

  const ConnectResult result = broker.handle_connect(connect, []() {});
  CHECK(result.reason_code == ReasonCode::Success);

  const auto server_keep_alive = find_two_byte_property(
      result.connack_properties, PropertyId::ServerKeepAlive);
  CHECK_FALSE(server_keep_alive.has_value());

  broker.shutdown();
}

TEST_CASE("broker_handle_connect_emits_info_trace", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.trace_global_level = TraceLevel::Info;

  Broker broker(cfg);
  broker.startup();

  std::ostringstream trace_output;
  broker.structured_tracer().set_output(trace_output);

  ConnectPacket connect;
  connect.client_id = Utf8String{"trace_client"};
  connect.clean_start = true;

  const ConnectResult result = broker.handle_connect(connect, []() {});
  CHECK(result.reason_code == ReasonCode::Success);

  const std::string trace_line = trace_output.str();
  CHECK(trace_line.find("\"module\":\"broker\"") != std::string::npos);
  CHECK(trace_line.find("\"info\":\"connect_handled\"") !=
        std::string::npos);
  CHECK(trace_line.find("\"level\":\"info\"") != std::string::npos);

  broker.shutdown();
}

TEST_CASE("broker_runtime_trace_system_message_updates_global_level",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.trace_global_level = TraceLevel::Warning;

  Broker broker(cfg);
  broker.startup();

  Message message;
  message.topic = Utf8String{"$SYS/broker/tracing/global"};
  message.payload = binary_from_text("trace");

  broker.apply_trace_system_message(message);
  CHECK(broker.structured_tracer().global_level() == TraceLevel::Trace);

  broker.shutdown();
}

TEST_CASE("broker_runtime_trace_system_message_updates_module_override",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.trace_global_level = TraceLevel::Error;

  Broker broker(cfg);
  broker.startup();

  CHECK_FALSE(
      broker.structured_tracer().should_emit(TraceLevel::Trace, "connection"));

  Message enable_message;
  enable_message.topic = Utf8String{"$SYS/broker/tracing/module/connection"};
  enable_message.payload = binary_from_text("trace");
  broker.apply_trace_system_message(enable_message);

  CHECK(broker.structured_tracer().should_emit(TraceLevel::Trace,
                                               "connection"));

  Message disable_message;
  disable_message.topic = Utf8String{"$SYS/broker/tracing/module/connection"};
  disable_message.payload = binary_from_text("none");
  broker.apply_trace_system_message(disable_message);

  CHECK_FALSE(
      broker.structured_tracer().should_emit(TraceLevel::Trace, "connection"));

  broker.shutdown();
}

  TEST_CASE("broker_runtime_trace_system_message_trims_payload_values",
        "[broker]") {
    BrokerConfig cfg = make_test_config();
    cfg.trace_global_level = TraceLevel::Error;

    Broker broker(cfg);
    broker.startup();

    Message set_global_message;
    set_global_message.topic = Utf8String{"$SYS/broker/tracing/global"};
    set_global_message.payload = binary_from_text("  info  ");
    broker.apply_trace_system_message(set_global_message);
    CHECK(broker.structured_tracer().global_level() == TraceLevel::Info);

    Message enable_module_message;
    enable_module_message.topic =
      Utf8String{"$SYS/broker/tracing/module/connection"};
    enable_module_message.payload = binary_from_text("  on  ");
    broker.apply_trace_system_message(enable_module_message);
    CHECK(broker.structured_tracer().should_emit(TraceLevel::Trace,
                           "connection"));

    Message disable_module_message;
    disable_module_message.topic =
      Utf8String{"$SYS/broker/tracing/module/connection"};
    disable_module_message.payload = binary_from_text("  off  ");
    broker.apply_trace_system_message(disable_module_message);
    CHECK_FALSE(broker.structured_tracer().should_emit(TraceLevel::Trace,
                             "connection"));

    broker.shutdown();
  }

  TEST_CASE("broker_runtime_trace_system_message_ignores_invalid_inputs",
        "[broker]") {
    BrokerConfig cfg = make_test_config();
    cfg.trace_global_level = TraceLevel::Warning;

    Broker broker(cfg);
    broker.startup();

    Message unknown_topic_message;
    unknown_topic_message.topic = Utf8String{"$SYS/broker/tracingx/global"};
    unknown_topic_message.payload = binary_from_text("trace");
    broker.apply_trace_system_message(unknown_topic_message);
    CHECK(broker.structured_tracer().global_level() == TraceLevel::Warning);

    Message empty_module_message;
    empty_module_message.topic = Utf8String{"$SYS/broker/tracing/module/"};
    empty_module_message.payload = binary_from_text("trace");
    broker.apply_trace_system_message(empty_module_message);
    CHECK_FALSE(
      broker.structured_tracer().should_emit(TraceLevel::Trace, "connection"));

    Message invalid_module_payload_message;
    invalid_module_payload_message.topic =
      Utf8String{"$SYS/broker/tracing/module/connection"};
    invalid_module_payload_message.payload = binary_from_text("invalid_payload");
    broker.apply_trace_system_message(invalid_module_payload_message);
    CHECK_FALSE(
      broker.structured_tracer().should_emit(TraceLevel::Trace, "connection"));

    Message invalid_global_payload_message;
    invalid_global_payload_message.topic =
      Utf8String{"$SYS/broker/tracing/global"};
    invalid_global_payload_message.payload = binary_from_text("verbose");
    broker.apply_trace_system_message(invalid_global_payload_message);
    CHECK(broker.structured_tracer().global_level() == TraceLevel::Warning);

    broker.shutdown();
  }

TEST_CASE("broker_handle_connect_empty_client_id_assigns_identifier",
    "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{""};

  const ConnectResult result = broker.handle_connect(connect, []() {});

  CHECK(result.reason_code == ReasonCode::Success);
  CHECK(result.session_present == false);
  CHECK_FALSE(result.client_id.empty());

  bool found_assigned_identifier = false;
  for (const Property &property : result.connack_properties) {
    if (property.id != PropertyId::AssignedClientIdentifier) {
      continue;
    }
    const Utf8String assigned = std::get<Utf8String>(property.value);
    CHECK_FALSE(assigned.value.empty());
    CHECK(assigned.value == result.client_id);
    found_assigned_identifier = true;
    break;
  }
  CHECK(found_assigned_identifier);

  broker.shutdown();
}

TEST_CASE("broker_handle_connect_request_response_information_adds_property",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"response-info-client"};
  connect.properties.push_back(
      Property{PropertyId::RequestResponseInformation, uint8_t{1U}});

  const ConnectResult result = broker.handle_connect(connect, []() {});

  CHECK(result.reason_code == ReasonCode::Success);
  bool found_response_information = false;
  for (const Property &property : result.connack_properties) {
    if (property.id != PropertyId::ResponseInformation) {
      continue;
    }
    const Utf8String value = std::get<Utf8String>(property.value);
    CHECK_FALSE(value.value.empty());
    found_response_information = true;
  }
  CHECK(found_response_information);

  broker.shutdown();
}

TEST_CASE("broker_handle_connect_empty_client_id_enhanced_auth_success",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  cfg.password_credentials = {
      PasswordCredentialConfig{.username = "user", .password = "secret"}};

  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{""};
  connect.properties.push_back(make_auth_method_property("PLAIN"));
  connect.properties.push_back(make_auth_data_property("user:secret"));

  const ConnectResult result = broker.handle_connect(connect, []() {});

  CHECK(result.reason_code == ReasonCode::Success);
  CHECK_FALSE(result.client_id.empty());
  bool found_assigned_identifier = false;
  for (const Property &property : result.connack_properties) {
    if (property.id != PropertyId::AssignedClientIdentifier) {
      continue;
    }
    const Utf8String assigned = std::get<Utf8String>(property.value);
    CHECK_FALSE(assigned.value.empty());
    CHECK(assigned.value == result.client_id);
    found_assigned_identifier = true;
  }
  CHECK(found_assigned_identifier);

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

  broker.register_connection("disc_client", std::make_shared<OutboundQueue>());
  CHECK(broker.statistics_collector().snapshot().connected_clients == 1U);

  broker.handle_disconnect("disc_client", ReasonCode::Success, std::nullopt,
                           std::chrono::steady_clock::now());
  CHECK(broker.statistics_collector().snapshot().connected_clients == 0U);

  broker.shutdown();
}

//
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

  broker.register_connection("c1", std::make_shared<OutboundQueue>());
  CHECK(broker.statistics_collector().snapshot().connected_clients == 1U);

  broker.register_connection("c2", std::make_shared<OutboundQueue>());
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

  broker.register_connection("same_client", std::make_shared<OutboundQueue>());
  CHECK(broker.statistics_collector().snapshot().connected_clients == 1U);

  broker.register_connection("same_client", std::make_shared<OutboundQueue>());
  CHECK(broker.statistics_collector().snapshot().connected_clients == 1U);

  broker.unregister_connection("same_client");
  CHECK(broker.statistics_collector().snapshot().connected_clients == 0U);

  broker.shutdown();
}

TEST_CASE("broker_register_same_client_transfers_pending_messages",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  auto first_queue = std::make_shared<OutboundQueue>();
  broker.register_connection("same_client", first_queue);

  Message pending_message;
  pending_message.topic = Utf8String{"xfer/topic"};
  pending_message.payload = BinaryData{{0x42U}};
  pending_message.qos = QoS::AtMostOnce;
  REQUIRE(first_queue->push(pending_message));

  auto replacement_queue = std::make_shared<OutboundQueue>();
  broker.register_connection("same_client", replacement_queue);

  REQUIRE(replacement_queue->size() == 1U);
  const auto moved_message = replacement_queue->try_pop();
  REQUIRE(moved_message.has_value());
  CHECK(moved_message->topic.value == "xfer/topic");

  broker.unregister_connection("same_client");
  broker.shutdown();
}

TEST_CASE("broker_handle_disconnect_with_mismatched_queue_keeps_connection",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  auto active_queue = std::make_shared<OutboundQueue>();
  broker.register_connection("disc_mismatch", active_queue);
  CHECK(broker.statistics_collector().snapshot().connected_clients == 1U);

  auto stale_queue = std::make_shared<OutboundQueue>();
  broker.handle_disconnect("disc_mismatch", ReasonCode::Success, std::nullopt,
                           std::chrono::steady_clock::now(), stale_queue);

  CHECK(broker.statistics_collector().snapshot().connected_clients == 1U);

  broker.unregister_connection("disc_mismatch");
  CHECK(broker.statistics_collector().snapshot().connected_clients == 0U);
  broker.shutdown();
}

TEST_CASE("broker_handle_publish_counts_inbound_via_facade", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.trace_global_level = TraceLevel::Trace;
  Broker broker(cfg);
  broker.startup();

  Message msg;
  msg.topic = Utf8String{"sensors/temp"};
  msg.qos = QoS::AtMostOnce;
  TopicAliasTable alias_table(0U);

  broker.handle_publish(msg, "pub_client", "", alias_table);
  CHECK(broker.statistics_collector().snapshot().messages_inbound == 1U);

  broker.handle_publish(msg, "pub_client", "", alias_table);
  CHECK(broker.statistics_collector().snapshot().messages_inbound == 2U);

  broker.shutdown();
}

TEST_CASE("broker_handle_publish_counts_inbound", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  Message msg;
  msg.topic = Utf8String{"sensors/pressure"};
  msg.qos = QoS::AtMostOnce;
  TopicAliasTable alias_table(0U);

  broker.handle_publish(msg, "pub_client", "", alias_table);
  CHECK(broker.statistics_collector().snapshot().messages_inbound == 1U);

  broker.handle_publish(msg, "pub_client", "", alias_table);
  CHECK(broker.statistics_collector().snapshot().messages_inbound == 2U);

  broker.shutdown();
}

TEST_CASE("broker_handle_publish_rejects_zero_topic_alias", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.trace_global_level = TraceLevel::Trace;
  Broker broker(cfg);
  broker.startup();

  Message message;
  message.topic = Utf8String{"sensors/alias"};
  message.qos = QoS::AtMostOnce;
  message.properties.push_back(
      Property{PropertyId::PayloadFormatIndicator, uint8_t{1U}});
  message.properties.push_back(Property{PropertyId::TopicAlias, uint16_t{0U}});
  TopicAliasTable alias_table(10U);

  const ReasonCode reason_code =
      broker.handle_publish(message, "pub_client", "", alias_table);
  CHECK(reason_code == ReasonCode::ImplementationSpecificError);

  broker.shutdown();
}

TEST_CASE("broker_handle_publish_maps_acl_rejection_to_not_authorized",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  Broker broker(cfg);
  broker.startup();

  Message message;
  message.topic = Utf8String{"restricted/topic"};
  message.qos = QoS::AtMostOnce;
  TopicAliasTable alias_table(10U);

  const ReasonCode reason_code =
      broker.handle_publish(message, "pub_client", "", alias_table);
  CHECK(reason_code == ReasonCode::NotAuthorized);

  broker.shutdown();
}

TEST_CASE("broker_handle_publish_maps_invalid_topic_alias_to_protocol_error",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  Message message;
  message.topic = Utf8String{""};
  message.qos = QoS::AtMostOnce;
  message.properties.push_back(Property{PropertyId::TopicAlias, uint16_t{1U}});
  TopicAliasTable alias_table(10U);

  const ReasonCode reason_code =
      broker.handle_publish(message, "pub_client", "", alias_table);
  CHECK(reason_code == ReasonCode::ProtocolError);

  broker.shutdown();
}

TEST_CASE("broker_handle_publish_maps_online_queue_full_to_quota_exceeded",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  auto subscriber_queue = std::make_shared<OutboundQueue>(1U);
  broker.register_connection("queue_full_sub", subscriber_queue);

  SubscribePacket subscribe_packet;
  subscribe_packet.packet_id = 21U;
  subscribe_packet.filters.push_back(
      SubscribeFilter{.topic_filter = Utf8String{"queue/full"},
                      .options = SubscribeOptions{.max_qos = QoS::AtMostOnce,
                                                  .no_local = false,
                                                  .retain_as_published = false,
                                                  .retain_handling = 0U}});
  const SubackPacket suback =
      broker.handle_subscribe("queue_full_sub", subscribe_packet);
  REQUIRE(suback.reason_codes.size() == 1U);
  REQUIRE(suback.reason_codes[0] == ReasonCode::Success);

  Message blocker_message;
  blocker_message.topic = Utf8String{"blocker"};
  blocker_message.qos = QoS::AtMostOnce;
  REQUIRE(subscriber_queue->push(blocker_message));

  Message message;
  message.topic = Utf8String{"queue/full"};
  message.payload = BinaryData{{0xABU}};
  message.qos = QoS::AtMostOnce;
  TopicAliasTable alias_table(10U);

  const ReasonCode reason_code =
      broker.handle_publish(message, "pub_client", "", alias_table);
  CHECK(reason_code == ReasonCode::QuotaExceeded);

  broker.shutdown();
}

TEST_CASE("broker_handle_publish_maps_frame_too_large_to_quota_exceeded",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.write_queue_max_bytes = 8U;
  Broker broker(cfg);
  broker.startup();

  auto subscriber_queue = std::make_shared<OutboundQueue>(10U);
  broker.register_connection("frame_limit_sub", subscriber_queue);

  SubscribePacket subscribe_packet;
  subscribe_packet.packet_id = 22U;
  subscribe_packet.filters.push_back(
      SubscribeFilter{.topic_filter = Utf8String{"queue/large"},
                      .options = SubscribeOptions{.max_qos = QoS::AtMostOnce,
                                                  .no_local = false,
                                                  .retain_as_published = false,
                                                  .retain_handling = 0U}});
  const SubackPacket suback =
      broker.handle_subscribe("frame_limit_sub", subscribe_packet);
  REQUIRE(suback.reason_codes.size() == 1U);
  REQUIRE(suback.reason_codes[0] == ReasonCode::Success);

  Message message;
  message.topic = Utf8String{"queue/large"};
  message.payload = BinaryData{{0x31U, 0x32U, 0x33U, 0x34U, 0x35U,
                                0x36U, 0x37U, 0x38U, 0x39U, 0x30U}};
  message.qos = QoS::AtMostOnce;
  TopicAliasTable alias_table(10U);

  const ReasonCode reason_code =
      broker.handle_publish(message, "pub_client", "", alias_table);
  CHECK(reason_code == ReasonCode::QuotaExceeded);

  broker.shutdown();
}

TEST_CASE("broker_handle_publish_with_null_registered_queue_is_safe", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  std::shared_ptr<OutboundQueue> null_queue;
  broker.register_connection("null_queue_sub", null_queue);

  SubscribePacket subscribe_packet;
  subscribe_packet.packet_id = 23U;
  subscribe_packet.filters.push_back(
      SubscribeFilter{.topic_filter = Utf8String{"null/queue"},
                      .options = SubscribeOptions{.max_qos = QoS::AtMostOnce,
                                                  .no_local = false,
                                                  .retain_as_published = false,
                                                  .retain_handling = 0U}});
  const SubackPacket suback =
      broker.handle_subscribe("null_queue_sub", subscribe_packet);
  REQUIRE(suback.reason_codes.size() == 1U);
  REQUIRE(suback.reason_codes[0] == ReasonCode::Success);

  Message message;
  message.topic = Utf8String{"null/queue"};
  message.qos = QoS::AtMostOnce;
  TopicAliasTable alias_table(10U);

  const ReasonCode reason_code =
      broker.handle_publish(message, "pub_client", "", alias_table);
  CHECK(reason_code == ReasonCode::Success);

  broker.shutdown();
}

TEST_CASE("broker_handle_subscribe_returns_suback_and_delivers_retained",
          "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  auto sub_queue = std::make_shared<OutboundQueue>();
  broker.register_connection("sub_client", sub_queue);

  Message retained_message;
  retained_message.topic = Utf8String{"alerts/high"};
  retained_message.payload = BinaryData{{0x41U}};
  retained_message.qos = QoS::AtLeastOnce;
  retained_message.retain = true;

  TopicAliasTable alias_table(0U);
  broker.handle_publish(retained_message, "pub_client", "", alias_table);

  SubscribePacket subscribe_packet;
  subscribe_packet.packet_id = 7U;
  subscribe_packet.filters.push_back(
      SubscribeFilter{.topic_filter = Utf8String{"alerts/#"},
                      .options = SubscribeOptions{.max_qos = QoS::AtLeastOnce,
                                                  .no_local = false,
                                                  .retain_as_published = false,
                                                  .retain_handling = 0U}});

  const SubackPacket suback =
      broker.handle_subscribe("sub_client", subscribe_packet);

  CHECK(suback.packet_id == 7U);
  REQUIRE(suback.reason_codes.size() == 1U);
  CHECK(suback.reason_codes[0] == ReasonCode::GrantedQoS1);
  REQUIRE(sub_queue->size() == 1U);
  auto popped_msg = sub_queue->try_pop();
  REQUIRE(popped_msg.has_value());
  CHECK(popped_msg->topic.value == "alerts/high");

  broker.shutdown();
}

TEST_CASE("broker_handle_subscribe_denied_returns_not_authorized", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.allow_anonymous = false;
  Broker broker(cfg);
  broker.startup();

  auto denied_queue = std::make_shared<OutboundQueue>();
  broker.register_connection("denied_sub_client", denied_queue);

  SubscribePacket subscribe_packet;
  subscribe_packet.packet_id = 9U;
  subscribe_packet.filters.push_back(
      SubscribeFilter{.topic_filter = Utf8String{"private/topic"},
                      .options = SubscribeOptions{.max_qos = QoS::AtMostOnce,
                                                  .no_local = false,
                                                  .retain_as_published = false,
                                                  .retain_handling = 0U}});

  const SubackPacket suback =
      broker.handle_subscribe("denied_sub_client", subscribe_packet);

  CHECK(suback.packet_id == 9U);
  REQUIRE(suback.reason_codes.size() == 1U);
  CHECK(suback.reason_codes[0] == ReasonCode::NotAuthorized);
  CHECK(denied_queue->is_empty());

  broker.shutdown();
}

TEST_CASE("broker_handle_unsubscribe_removes_subscription", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  auto unsub_queue = std::make_shared<OutboundQueue>();
  broker.register_connection("sub_remove_client", unsub_queue);

  SubscribePacket subscribe_packet;
  subscribe_packet.packet_id = 11U;
  subscribe_packet.filters.push_back(
      SubscribeFilter{.topic_filter = Utf8String{"devices/+/status"},
                      .options = SubscribeOptions{.max_qos = QoS::AtMostOnce,
                                                  .no_local = false,
                                                  .retain_as_published = false,
                                                  .retain_handling = 0U}});
  const SubackPacket suback =
      broker.handle_subscribe("sub_remove_client", subscribe_packet);
  REQUIRE(suback.reason_codes.size() == 1U);
  CHECK(suback.reason_codes[0] == ReasonCode::Success);

  Message first_message;
  first_message.topic = Utf8String{"devices/a/status"};
  first_message.payload = BinaryData{{0x10U}};
  first_message.qos = QoS::AtMostOnce;
  TopicAliasTable alias_table(0U);
  broker.handle_publish(first_message, "pub_client", "", alias_table);
  REQUIRE(unsub_queue->size() == 1U);

  UnsubscribePacket unsubscribe_packet;
  unsubscribe_packet.packet_id = 12U;
  unsubscribe_packet.topic_filters.push_back(Utf8String{"devices/+/status"});
  const UnsubackPacket unsuback =
      broker.handle_unsubscribe("sub_remove_client", unsubscribe_packet);

  CHECK(unsuback.packet_id == 12U);
  REQUIRE(unsuback.reason_codes.size() == 1U);
  CHECK(unsuback.reason_codes[0] == ReasonCode::Success);

  Message second_message;
  second_message.topic = Utf8String{"devices/b/status"};
  second_message.payload = BinaryData{{0x11U}};
  second_message.qos = QoS::AtMostOnce;
  broker.handle_publish(second_message, "pub_client", "", alias_table);
  CHECK(unsub_queue->size() == 1U);

  broker.shutdown();
}

TEST_CASE("broker_unregister_unknown_client_keeps_count", "[broker]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  broker.unregister_connection("missing_client");
  CHECK(broker.statistics_collector().snapshot().connected_clients == 0U);

  broker.register_connection("present_client",
                             std::make_shared<OutboundQueue>());
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

TEST_CASE("broker_tick_handles_session_expiry_and_will_publish", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.sys_topic_interval = 0U;
  Broker broker(cfg);
  broker.startup();

  auto subscriber_queue = std::make_shared<OutboundQueue>();
  broker.register_connection("sub_client", subscriber_queue);

  SubscribePacket subscribe_packet;
  subscribe_packet.packet_id = 40U;
  subscribe_packet.filters.push_back(
      SubscribeFilter{.topic_filter = Utf8String{"will/topic"},
                      .options = SubscribeOptions{.max_qos = QoS::AtMostOnce,
                                                  .no_local = false,
                                                  .retain_as_published = false,
                                                  .retain_handling = 0U}});
  const SubackPacket suback =
      broker.handle_subscribe("sub_client", subscribe_packet);
  REQUIRE(suback.reason_codes.size() == 1U);
  CHECK(suback.reason_codes[0] == ReasonCode::Success);

  ConnectPacket connect_packet;
  connect_packet.client_id = Utf8String{"expiring_client"};
  connect_packet.properties.push_back(
      Property{PropertyId::SessionExpiryInterval, FourByteInteger{1U}});

  WillData will_data;
  will_data.topic = Utf8String{"will/topic"};
  will_data.payload = BinaryData{{0x33U}};
  will_data.qos = QoS::AtMostOnce;
  will_data.retain = false;
  will_data.properties.push_back(
      Property{PropertyId::WillDelayInterval, FourByteInteger{30U}});
  connect_packet.will = will_data;

  const ConnectResult connect_result =
      broker.handle_connect(connect_packet, []() {});
  REQUIRE(connect_result.reason_code == ReasonCode::Success);

  const auto disconnect_time = std::chrono::steady_clock::now();
  broker.handle_connection_lost("expiring_client", disconnect_time);
  CHECK(subscriber_queue->is_empty());

  CHECK_FALSE(broker.tick(disconnect_time + 2s));
  REQUIRE(subscriber_queue->size() == 1U);
  const auto delivered_message = subscriber_queue->try_pop();
  REQUIRE(delivered_message.has_value());
  CHECK(delivered_message->topic.value == "will/topic");

  broker.shutdown();
}

TEST_CASE("broker_tick_with_no_housekeeping_work_is_safe", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.sys_topic_interval = 0U;
  Broker broker(cfg);
  broker.startup();

  CHECK_FALSE(broker.tick(std::chrono::steady_clock::now()));

  broker.shutdown();
}
