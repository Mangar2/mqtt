#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <optional>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "broker/core/broker.h"
#include "broker/core/broker_config.h"
#include "broker/connection/topic_alias_table.h"
#include "broker/data_model/message/message.h"
#include "broker/data_model/packet/connect_packet.h"
#include "broker/data_model/property/property_id.h"
#include "broker/data_model/reason_code/reason_code.h"
#include "broker/data_model/types/qos.h"
#include "broker/data_model/types/utf8_string.h"
#include "broker/outbound_queue/outbound_queue.h"

using namespace mqtt;
using namespace std::chrono_literals;

//
// Helpers

namespace {

constexpr uint16_t k_test_port_base = 18883U;
constexpr uint32_t k_port_scan_attempts = 3000U;
constexpr uint32_t k_port_max_exclusive = 60999U;
constexpr uint32_t k_port_wrap_window = 1000U;

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

} // namespace

//
// Monitoring — Module 16 integration

// NOLINTBEGIN(readability-magic-numbers)

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
      Property{.id = PropertyId::PayloadFormatIndicator, .value = uint8_t{1U}});
    message.properties.push_back(
      Property{.id = PropertyId::TopicAlias, .value = uint16_t{0U}});
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
  message.properties.push_back(
      Property{.id = PropertyId::TopicAlias, .value = uint16_t{1U}});
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
              .options = SubscribeOptions{.max_qos = QoS::AtLeastOnce,
                                                  .no_local = false,
                                                  .retain_as_published = false,
                                                  .retain_handling = 0U}});
  const SubackPacket suback =
      broker.handle_subscribe("queue_full_sub", subscribe_packet);
  REQUIRE(suback.reason_codes.size() == 1U);
  REQUIRE(is_success(suback.reason_codes[0]));

  Message blocker_message;
  blocker_message.topic = Utf8String{"blocker"};
  blocker_message.qos = QoS::AtMostOnce;
  REQUIRE(subscriber_queue->push(blocker_message));

  Message message;
  message.topic = Utf8String{"queue/full"};
  message.payload = BinaryData{{0xABU}};
  // Queue-full is mapped to QuotaExceeded for QoS 1/2 publish handling.
  // QoS 0 is intentionally dropped silently.
  message.qos = QoS::AtLeastOnce;
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
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
      Property{.id = PropertyId::SessionExpiryInterval,
           .value = FourByteInteger{1U}});

  WillData will_data;
  will_data.topic = Utf8String{"will/topic"};
  will_data.payload = BinaryData{{0x33U}};
  will_data.qos = QoS::AtMostOnce;
  will_data.retain = false;
  will_data.properties.push_back(
      Property{.id = PropertyId::WillDelayInterval,
           .value = FourByteInteger{30U}});
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

// NOLINTEND(readability-magic-numbers)
