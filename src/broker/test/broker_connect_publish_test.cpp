#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <optional>
#include <sstream>
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
#include "data_model/message/message.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/property/property_id.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"
#include "network/tcp_connection.h"
#include "outbound_queue/outbound_queue.h"

using namespace mqtt;
using namespace std::chrono_literals;

//
// Helpers

namespace {

constexpr uint16_t k_test_port_base = 18883U;
constexpr uint32_t k_port_scan_attempts = 3000U;
constexpr uint32_t k_port_max_exclusive = 60999U;
constexpr uint32_t k_port_wrap_window = 1000U;
constexpr TwoByteInteger k_connack_receive_maximum = 123U;
constexpr TwoByteInteger k_connack_server_keep_alive = 9U;
constexpr TwoByteInteger k_connack_topic_alias_maximum = 77U;
constexpr uint8_t k_property_enabled = 1U;
constexpr uint8_t k_payload_byte_a = 0x41U;
constexpr uint8_t k_payload_byte_b = 0x42U;
constexpr FourByteInteger k_will_delay_interval = 15U;

SocketHandle create_tcp_socket() {
  return static_cast<SocketHandle>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
}

void close_socket_handle(mqtt::SocketHandle socket_handle) {
#ifdef _WIN32
  ::closesocket(static_cast<SOCKET>(socket_handle));
#else
  ::close(static_cast<int>(socket_handle));
#endif
}

bool can_bind_loopback_port(uint16_t port_value) {
  const SocketHandle probe_socket = create_tcp_socket();
  if (probe_socket == mqtt::k_invalid_socket) {
    return false;
  }

  int reuse_addr = 1;
#ifdef _WIN32
  (void)::setsockopt(static_cast<SOCKET>(probe_socket), SOL_SOCKET,
                     SO_REUSEADDR,
                     reinterpret_cast<const char *>(&reuse_addr),
                     static_cast<int>(sizeof(reuse_addr)));
#else
  (void)::setsockopt(static_cast<int>(probe_socket), SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char *>(&reuse_addr),
                     static_cast<socklen_t>(sizeof(reuse_addr)));
#endif

  sockaddr_in probe_addr{};
  probe_addr.sin_family = AF_INET;
  probe_addr.sin_port = htons(port_value);
  probe_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

#ifdef _WIN32
  const int bind_result =
      ::bind(static_cast<SOCKET>(probe_socket),
             reinterpret_cast<const sockaddr *>(&probe_addr),
             static_cast<int>(sizeof(probe_addr)));
#else
  const int bind_result =
      ::bind(static_cast<int>(probe_socket),
             reinterpret_cast<const sockaddr *>(&probe_addr),
             static_cast<socklen_t>(sizeof(probe_addr)));
#endif
  close_socket_handle(probe_socket);
  return bind_result == 0;
}

uint16_t next_test_port() {
  static std::atomic<uint32_t> next_port_candidate{k_test_port_base};

  for (uint32_t attempt_index = 0; attempt_index < k_port_scan_attempts;
       ++attempt_index) {
    uint32_t candidate_port =
        next_port_candidate.fetch_add(1U, std::memory_order_relaxed);
    if (candidate_port >= k_port_max_exclusive) {
      const uint32_t wrapped_port =
          k_test_port_base + (candidate_port % k_port_wrap_window);
      candidate_port = wrapped_port;
    }

    if (can_bind_loopback_port(static_cast<uint16_t>(candidate_port))) {
      return static_cast<uint16_t>(candidate_port);
    }
  }

  return k_test_port_base;
}

/// Build a BrokerConfig that binds a single MQTT TCP listener on a
/// high-numbered ephemeral test port selected at runtime.
BrokerConfig make_test_config() {
  BrokerConfig cfg;
  cfg.mqtt_port = next_test_port();
  cfg.ws_port = 0U;       // disabled
  cfg.allow_anonymous = true;
  cfg.persistence_mode = PersistenceMode::Off;
  return cfg;
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
  return Property{.id = PropertyId::AuthenticationMethod,
                  .value = Utf8String{std::string(method_name)}};
}

Property make_auth_data_property(std::string_view payload_text) {
  return Property{.id = PropertyId::AuthenticationData,
                  .value = binary_from_text(payload_text)};
}

} // namespace

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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("broker_handle_connect_builds_connack_properties", "[broker]") {
  BrokerConfig cfg = make_test_config();
  cfg.receive_maximum = k_connack_receive_maximum;
  cfg.server_keep_alive = k_connack_server_keep_alive;
  cfg.topic_alias_maximum = k_connack_topic_alias_maximum;
  Broker broker(cfg);
  broker.startup();

  ConnectPacket connect;
  connect.client_id = Utf8String{"connack_prop_client"};

  const ConnectResult result = broker.handle_connect(connect, []() {});

  CHECK(result.reason_code == ReasonCode::Success);
  const auto receive_maximum = find_two_byte_property(
      result.connack_properties, PropertyId::ReceiveMaximum);
  REQUIRE(receive_maximum.has_value());
  CHECK(*receive_maximum == k_connack_receive_maximum);

  const auto topic_alias_maximum = find_two_byte_property(
      result.connack_properties, PropertyId::TopicAliasMaximum);
  REQUIRE(topic_alias_maximum.has_value());
  CHECK(*topic_alias_maximum == k_connack_topic_alias_maximum);

    const auto server_keep_alive = find_two_byte_property(
      result.connack_properties, PropertyId::ServerKeepAlive);
    REQUIRE(server_keep_alive.has_value());
    CHECK(*server_keep_alive == k_connack_server_keep_alive);

    const auto maximum_qos =
      find_byte_property(result.connack_properties, PropertyId::MaximumQoS);
    CHECK_FALSE(maximum_qos.has_value());

    const auto retain_available =
      find_byte_property(result.connack_properties, PropertyId::RetainAvailable);
    REQUIRE(retain_available.has_value());
    CHECK(*retain_available == k_property_enabled);

    const auto maximum_packet_size = find_four_byte_property(
      result.connack_properties, PropertyId::MaximumPacketSize);
    REQUIRE(maximum_packet_size.has_value());
    CHECK(*maximum_packet_size == 0x0FFFFFFFU);

    const auto wildcard_subscription_available = find_byte_property(
      result.connack_properties, PropertyId::WildcardSubscriptionAvailable);
    REQUIRE(wildcard_subscription_available.has_value());
    CHECK(*wildcard_subscription_available == k_property_enabled);

    const auto subscription_identifier_available = find_byte_property(
      result.connack_properties, PropertyId::SubscriptionIdentifierAvailable);
    REQUIRE(subscription_identifier_available.has_value());
    CHECK(*subscription_identifier_available == k_property_enabled);

    const auto shared_subscription_available = find_byte_property(
      result.connack_properties, PropertyId::SharedSubscriptionAvailable);
    REQUIRE(shared_subscription_available.has_value());
    CHECK(*shared_subscription_available == k_property_enabled);

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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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
      Property{.id = PropertyId::RequestResponseInformation,
           .value = uint8_t{k_property_enabled}});

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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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
    will.payload = BinaryData{{k_payload_byte_a, k_payload_byte_b}};
  will.qos = QoS::AtLeastOnce;
  will.retain = true;
    will.properties.push_back(Property{.id = PropertyId::WillDelayInterval,
                     .value = k_will_delay_interval});
  will.properties.push_back(
      Property{.id = PropertyId::ContentType,
           .value = Utf8String{"application/octet-stream"}});
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

