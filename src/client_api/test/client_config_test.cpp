#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "client_api/async_client.h"
#include "client_api/client_config.h"
#include "client_api/sync_client.h"

namespace mqtt {

namespace {

[[nodiscard]] bool wait_until_true(std::condition_variable &condition_variable,
                                   std::mutex &state_mutex,
                                   bool &state_value) {
  std::unique_lock<std::mutex> state_lock(state_mutex);
  return condition_variable.wait_for(
      state_lock, std::chrono::milliseconds{3000},
      [&state_value]() { return state_value; });
}

[[nodiscard]] ConnectionNegotiationResult make_connect_result() {
  ConnectionNegotiationResult connect_result;
  connect_result.session_present = false;
  connect_result.reason_code = ReasonCode::Success;
  return connect_result;
}

[[nodiscard]] Message make_qos1_message() {
  Message message;
  message.topic = Utf8String{"sensor/qos1"};
  message.qos = QoS::AtLeastOnce;
  message.payload = BinaryData{{0x44U}};
  return message;
}

} // namespace

TEST_CASE("client_config_defaults_are_sensible", "[client_api][config]") {
  const ClientConfig client_config;

  CHECK(client_config.broker_host == "127.0.0.1");
  CHECK(client_config.broker_port == 1883U);
  CHECK(client_config.transport == ClientTransportType::Tcp);
  CHECK(client_config.client_id == "mqtt-client");
  CHECK(client_config.clean_start);
  CHECK(client_config.keep_alive_seconds == 30U);
  CHECK(client_config.receive_maximum == 65535U);
  CHECK(client_config.operation_timeouts.connect_ms > 0U);
  CHECK(client_config.operation_timeouts.publish_ms > 0U);
  CHECK(client_config.operation_timeouts.subscribe_ms > 0U);
  CHECK(client_config.operation_timeouts.unsubscribe_ms > 0U);
  CHECK(client_config.operation_timeouts.disconnect_ms > 0U);

  CHECK_NOTHROW(validate_client_config_or_throw(client_config));
}

TEST_CASE("client_config_validate_rejects_invalid_values",
          "[client_api][config]") {
  ClientConfig empty_host_config;
  empty_host_config.broker_host.clear();
  CHECK_THROWS_AS(validate_client_config_or_throw(empty_host_config),
                  ClientException);

  ClientConfig empty_client_identifier_config;
  empty_client_identifier_config.client_id.clear();
  CHECK_THROWS_AS(validate_client_config_or_throw(empty_client_identifier_config),
                  ClientException);

  ClientConfig zero_port_config;
  zero_port_config.broker_port = 0U;
  CHECK_THROWS_AS(validate_client_config_or_throw(zero_port_config),
                  ClientException);

  ClientConfig password_without_username_config;
  password_without_username_config.credentials.password = "secret";
  CHECK_THROWS_AS(
      validate_client_config_or_throw(password_without_username_config),
      ClientException);

  ClientConfig zero_receive_maximum_config;
  zero_receive_maximum_config.receive_maximum = 0U;
  CHECK_THROWS_AS(validate_client_config_or_throw(zero_receive_maximum_config),
                  ClientException);

  ClientConfig zero_timeout_config;
  zero_timeout_config.operation_timeouts.publish_ms = 0U;
  CHECK_THROWS_AS(validate_client_config_or_throw(zero_timeout_config),
                  ClientException);
}

TEST_CASE("client_config_build_connect_packet_maps_credentials_and_properties",
          "[client_api][config]") {
  ClientConfig client_config;
  client_config.client_id = "cfg-client";
  client_config.clean_start = false;
  client_config.keep_alive_seconds = 45U;
  client_config.credentials.username = "user";
  client_config.credentials.password = "pw";
  client_config.session_expiry_interval_seconds = 120U;
  client_config.receive_maximum = 42U;
  client_config.topic_alias_maximum = 9U;

  const ConnectPacket connect_packet = build_connect_packet(client_config);

  CHECK(connect_packet.client_id.value == "cfg-client");
  CHECK_FALSE(connect_packet.clean_start);
  CHECK(connect_packet.keep_alive == 45U);
  REQUIRE(connect_packet.username.has_value());
  CHECK(connect_packet.username->value == "user");
  REQUIRE(connect_packet.password.has_value());
  REQUIRE(connect_packet.password->data.size() == 2U);
  CHECK(connect_packet.password->data[0] == static_cast<uint8_t>('p'));
  CHECK(connect_packet.password->data[1] == static_cast<uint8_t>('w'));

  bool has_receive_maximum = false;
  bool has_topic_alias_maximum = false;
  bool has_session_expiry = false;

  for (const Property &property : connect_packet.properties) {
    if (property.id == PropertyId::ReceiveMaximum) {
      has_receive_maximum = true;
      CHECK(std::get<TwoByteInteger>(property.value) == 42U);
    }
    if (property.id == PropertyId::TopicAliasMaximum) {
      has_topic_alias_maximum = true;
      CHECK(std::get<TwoByteInteger>(property.value) == 9U);
    }
    if (property.id == PropertyId::SessionExpiryInterval) {
      has_session_expiry = true;
      CHECK(std::get<FourByteInteger>(property.value) == 120U);
    }
  }

  CHECK(has_receive_maximum);
  CHECK(has_topic_alias_maximum);
  CHECK(has_session_expiry);
}

TEST_CASE("sync_client_constructed_from_config_uses_configured_connect_defaults",
          "[client_api][config]") {
  ClientConfig client_config;
  client_config.client_id = "sync-config-client";
  client_config.clean_start = false;
  client_config.keep_alive_seconds = 33U;
  client_config.operation_timeouts.connect_ms = 3210U;

  SyncClient client(client_config);
  SyncClientCallbacks callbacks;
  uint32_t observed_connect_timeout = 0U;
  ConnectPacket observed_connect_packet;
  callbacks.connect_and_negotiate =
      [&observed_connect_timeout,
       &observed_connect_packet](const ConnectPacket &connect_packet,
                                 uint32_t timeout_ms) {
        observed_connect_timeout = timeout_ms;
        observed_connect_packet = connect_packet;
        return make_connect_result();
      };
  client.set_callbacks(std::move(callbacks));

  const ConnectionNegotiationResult connect_result = client.connect();
  CHECK(connect_result.reason_code == ReasonCode::Success);
  CHECK(observed_connect_timeout == 3210U);
  CHECK(observed_connect_packet.client_id.value == "sync-config-client");
  CHECK_FALSE(observed_connect_packet.clean_start);
  CHECK(observed_connect_packet.keep_alive == 33U);
  CHECK(client.client_config().client_id == "sync-config-client");
}

TEST_CASE("async_client_default_overloads_use_configured_timeouts",
          "[client_api][config]") {
  ClientConfig client_config;
  client_config.client_id = "async-config-client";
  client_config.operation_timeouts.connect_ms = 1100U;
  client_config.operation_timeouts.publish_ms = 2200U;
  client_config.operation_timeouts.subscribe_ms = 3300U;
  client_config.operation_timeouts.unsubscribe_ms = 4400U;

  AsyncClient client(client_config);

  uint32_t observed_connect_timeout = 0U;
  uint32_t observed_publish_timeout = 0U;
  uint32_t observed_subscribe_timeout = 0U;
  uint32_t observed_unsubscribe_timeout = 0U;

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate =
      [&observed_connect_timeout](const ConnectPacket &, uint32_t timeout_ms) {
        observed_connect_timeout = timeout_ms;
        return make_connect_result();
      };
  callbacks.send_publish = [](const PublishPacket &) {};
  callbacks.wait_puback = [&observed_publish_timeout](uint16_t packet_id,
                                                      uint32_t timeout_ms) {
    observed_publish_timeout = timeout_ms;
    return PubackPacket{
        .packet_id = packet_id,
        .reason_code = ReasonCode::Success,
        .properties = {}};
  };
  callbacks.send_subscribe = [](const SubscribePacket &) {};
  callbacks.wait_suback = [&observed_subscribe_timeout](uint16_t packet_id,
                                                        uint32_t timeout_ms) {
    observed_subscribe_timeout = timeout_ms;
    return SubackPacket{.packet_id = packet_id,
                        .properties = {},
                        .reason_codes = {ReasonCode::GrantedQoS1}};
  };
  callbacks.send_unsubscribe = [](const UnsubscribePacket &) {};
  callbacks.wait_unsuback =
      [&observed_unsubscribe_timeout](uint16_t packet_id, uint32_t timeout_ms) {
        observed_unsubscribe_timeout = timeout_ms;
        return UnsubackPacket{.packet_id = packet_id,
                              .properties = {},
                              .reason_codes = {ReasonCode::Success}};
      };
  client.set_sync_callbacks(std::move(callbacks));

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool connect_done = false;
  bool publish_done = false;
  bool subscribe_done = false;
  bool unsubscribe_done = false;

  client.async_connect([&callback_mutex, &callback_condition,
                        &connect_done](
                           const std::optional<ConnectionNegotiationResult> &result,
                           const std::optional<AsyncOperationError> &operation_error) {
    CHECK(result.has_value());
    CHECK_FALSE(operation_error.has_value());
    {
      std::lock_guard<std::mutex> callback_guard(callback_mutex);
      connect_done = true;
    }
    callback_condition.notify_one();
  });
  REQUIRE(wait_until_true(callback_condition, callback_mutex, connect_done));

  client.async_publish(
      make_qos1_message(),
      [&callback_mutex, &callback_condition,
       &publish_done](const std::optional<ReasonCode> &result,
                      const std::optional<AsyncOperationError> &operation_error) {
        CHECK(result.has_value());
        CHECK_FALSE(operation_error.has_value());
        {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          publish_done = true;
        }
        callback_condition.notify_one();
      });
  REQUIRE(wait_until_true(callback_condition, callback_mutex, publish_done));

  const AsyncSubscribeRequest subscribe_request{
      .topic_filter = "sensor/#",
      .requested_qos = QoS::AtLeastOnce,
      .options = {}};
  client.async_subscribe(
      {subscribe_request},
      [&callback_mutex, &callback_condition,
       &subscribe_done](const std::optional<std::vector<ReasonCode>> &result,
                        const std::optional<AsyncOperationError> &operation_error) {
        REQUIRE(result.has_value());
        CHECK_FALSE(operation_error.has_value());
        {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          subscribe_done = true;
        }
        callback_condition.notify_one();
      });
  REQUIRE(wait_until_true(callback_condition, callback_mutex, subscribe_done));

  client.async_unsubscribe(
      {"sensor/#"},
      [&callback_mutex, &callback_condition,
       &unsubscribe_done](const std::optional<std::vector<ReasonCode>> &result,
                          const std::optional<AsyncOperationError> &operation_error) {
        REQUIRE(result.has_value());
        CHECK_FALSE(operation_error.has_value());
        {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          unsubscribe_done = true;
        }
        callback_condition.notify_one();
      });
  REQUIRE(wait_until_true(callback_condition, callback_mutex, unsubscribe_done));

  CHECK(observed_connect_timeout == 1100U);
  CHECK(observed_publish_timeout == 2200U);
  CHECK(observed_subscribe_timeout == 3300U);
  CHECK(observed_unsubscribe_timeout == 4400U);
  CHECK(client.client_config().client_id == "async-config-client");
}

} // namespace mqtt
