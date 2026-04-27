#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include "client_api/async_client.h"

namespace mqtt {

namespace {

using SteadyClock = std::chrono::steady_clock;

[[nodiscard]] ConnectPacket make_connect_packet() {
  ConnectPacket connect_packet;
  connect_packet.client_id = Utf8String{"async-client"};
  connect_packet.clean_start = true;
  connect_packet.keep_alive = 20U;
  return connect_packet;
}

[[nodiscard]] ConnectionNegotiationResult make_connect_result() {
  ConnectionNegotiationResult connect_result;
  connect_result.session_present = false;
  connect_result.reason_code = ReasonCode::Success;
  return connect_result;
}

[[nodiscard]] Message make_message(std::string topic_filter, QoS qos_level) {
  Message message;
  message.topic = Utf8String{std::move(topic_filter)};
  message.qos = qos_level;
  message.payload = BinaryData{{0x41U, 0x42U}};
  return message;
}

[[nodiscard]] PublishPacket make_inbound_publish_packet(std::string topic_name) {
  PublishPacket publish_packet;
  publish_packet.topic = Utf8String{std::move(topic_name)};
  publish_packet.qos = QoS::AtMostOnce;
  publish_packet.payload = BinaryData{{0x33U}};
  return publish_packet;
}

[[nodiscard]] bool wait_until_true(std::condition_variable &condition_variable,
                                   std::mutex &state_mutex,
                                   bool &state_value) {
  std::unique_lock<std::mutex> state_lock(state_mutex);
  return condition_variable.wait_for(
      state_lock, std::chrono::milliseconds{3000},
      [&state_value]() { return state_value; });
}

} // namespace

TEST_CASE("async_client_connect_completes_on_dispatch_thread",
          "[client_api][async]") {
  AsyncClient client("async-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_connect_result();
  };
  client.set_sync_callbacks(std::move(callbacks));

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool completion_called = false;
  std::thread::id callback_thread_id;
  const std::thread::id test_thread_id = std::this_thread::get_id();

  client.async_connect(
      make_connect_packet(),
      [&callback_mutex, &callback_condition, &completion_called,
       &callback_thread_id](
          const std::optional<ConnectionNegotiationResult> &result,
          const std::optional<AsyncOperationError> &operation_error) {
        CHECK(result.has_value());
        CHECK_FALSE(operation_error.has_value());
        {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          completion_called = true;
          callback_thread_id = std::this_thread::get_id();
        }
        callback_condition.notify_one();
      },
      1111U);

  REQUIRE(wait_until_true(callback_condition, callback_mutex, completion_called));
  CHECK(callback_thread_id != test_thread_id);
  CHECK(client.is_connected());
}

TEST_CASE("async_client_publish_qos0_invokes_completion_and_send_callback",
          "[client_api][async]") {
  AsyncClient client("async-client");

  std::size_t publish_send_count = 0U;
  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_connect_result();
  };
  callbacks.send_publish = [&publish_send_count](const PublishPacket &) {
    ++publish_send_count;
  };
  client.set_sync_callbacks(std::move(callbacks));

  std::mutex connect_mutex;
  std::condition_variable connect_condition;
  bool connect_done = false;
  client.async_connect(
      make_connect_packet(),
      [&connect_mutex, &connect_condition,
       &connect_done](const std::optional<ConnectionNegotiationResult> &,
                      const std::optional<AsyncOperationError> &) {
        {
          std::lock_guard<std::mutex> connect_guard(connect_mutex);
          connect_done = true;
        }
        connect_condition.notify_one();
      });
  REQUIRE(wait_until_true(connect_condition, connect_mutex, connect_done));

  std::mutex publish_mutex;
  std::condition_variable publish_condition;
  bool publish_done = false;
  ReasonCode publish_reason_code = ReasonCode::UnspecifiedError;

  client.async_publish(
      make_message("topic/async/qos0", QoS::AtMostOnce),
      [&publish_mutex, &publish_condition, &publish_done,
       &publish_reason_code](const std::optional<ReasonCode> &result,
                             const std::optional<AsyncOperationError> &
                                 operation_error) {
        CHECK(result.has_value());
        CHECK_FALSE(operation_error.has_value());
        {
          std::lock_guard<std::mutex> publish_guard(publish_mutex);
          publish_done = true;
          publish_reason_code = *result;
        }
        publish_condition.notify_one();
      },
      2500U);

  REQUIRE(wait_until_true(publish_condition, publish_mutex, publish_done));
  CHECK(publish_reason_code == ReasonCode::Success);
  CHECK(publish_send_count == 1U);
}

TEST_CASE("async_client_subscribe_then_inbound_publish_calls_message_handler",
          "[client_api][async]") {
  AsyncClient client("async-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_connect_result();
  };
  callbacks.send_subscribe = [](const SubscribePacket &) {};
  callbacks.wait_suback = [](uint16_t packet_identifier, uint32_t) {
    return SubackPacket{.packet_id = packet_identifier,
                        .properties = {},
                        .reason_codes = {ReasonCode::GrantedQoS1}};
  };
  client.set_sync_callbacks(std::move(callbacks));

  std::mutex message_mutex;
  std::condition_variable message_condition;
  bool message_received = false;
  std::thread::id message_thread_id;
  std::string received_topic;
  client.set_message_handler(
      [&message_mutex, &message_condition, &message_received, &message_thread_id,
       &received_topic](const PublishPacket &publish_packet) {
        {
          std::lock_guard<std::mutex> message_guard(message_mutex);
          message_received = true;
          message_thread_id = std::this_thread::get_id();
          received_topic = publish_packet.topic.value;
        }
        message_condition.notify_one();
      });

  std::mutex connect_mutex;
  std::condition_variable connect_condition;
  bool connect_done = false;
  client.async_connect(
      make_connect_packet(),
      [&connect_mutex, &connect_condition,
       &connect_done](const std::optional<ConnectionNegotiationResult> &,
                      const std::optional<AsyncOperationError> &) {
        {
          std::lock_guard<std::mutex> connect_guard(connect_mutex);
          connect_done = true;
        }
        connect_condition.notify_one();
      });
  REQUIRE(wait_until_true(connect_condition, connect_mutex, connect_done));

  std::mutex subscribe_mutex;
  std::condition_variable subscribe_condition;
  bool subscribe_done = false;
  std::thread::id subscribe_thread_id;

  const AsyncSubscribeRequest subscribe_request{
      .topic_filter = "sensor/+/temperature",
      .requested_qos = QoS::AtLeastOnce,
      .options = {}};
  client.async_subscribe(
      {subscribe_request},
      [&subscribe_mutex, &subscribe_condition, &subscribe_done,
       &subscribe_thread_id](const std::optional<std::vector<ReasonCode>> &result,
                             const std::optional<AsyncOperationError> &
                                 operation_error) {
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 1U);
        CHECK(result->front() == ReasonCode::GrantedQoS1);
        CHECK_FALSE(operation_error.has_value());
        {
          std::lock_guard<std::mutex> subscribe_guard(subscribe_mutex);
          subscribe_done = true;
          subscribe_thread_id = std::this_thread::get_id();
        }
        subscribe_condition.notify_one();
      });

  REQUIRE(wait_until_true(subscribe_condition, subscribe_mutex, subscribe_done));
  CHECK(client.has_subscription("sensor/+/temperature"));

  client.on_inbound_publish(make_inbound_publish_packet("sensor/lab/temperature"));

  REQUIRE(wait_until_true(message_condition, message_mutex, message_received));
  CHECK(received_topic == "sensor/lab/temperature");
  CHECK(message_thread_id == subscribe_thread_id);
}

TEST_CASE(
    "async_client_unsubscribe_reports_error_result_without_state_regression",
    "[client_api][async]") {
  AsyncClient client("async-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_connect_result();
  };
  callbacks.send_subscribe = [](const SubscribePacket &) {};
  callbacks.wait_suback = [](uint16_t packet_identifier, uint32_t) {
    return SubackPacket{.packet_id = packet_identifier,
                        .properties = {},
                        .reason_codes = {ReasonCode::GrantedQoS1}};
  };
  callbacks.send_unsubscribe = [](const UnsubscribePacket &) {};
  callbacks.wait_unsuback = [](uint16_t packet_identifier, uint32_t) {
    return UnsubackPacket{.packet_id = packet_identifier,
                          .properties = {},
                          .reason_codes = {ReasonCode::NotAuthorized}};
  };
  client.set_sync_callbacks(std::move(callbacks));

  std::mutex completion_mutex;
  std::condition_variable completion_condition;
  bool connect_done = false;
  bool subscribe_done = false;
  bool unsubscribe_done = false;
  std::optional<AsyncOperationError> unsubscribe_error;

  client.async_connect(
      make_connect_packet(),
      [&completion_mutex, &completion_condition,
       &connect_done](const std::optional<ConnectionNegotiationResult> &,
                      const std::optional<AsyncOperationError> &) {
        {
          std::lock_guard<std::mutex> completion_guard(completion_mutex);
          connect_done = true;
        }
        completion_condition.notify_one();
      });
  REQUIRE(wait_until_true(completion_condition, completion_mutex, connect_done));

  const AsyncSubscribeRequest subscribe_request{
      .topic_filter = "blocked/topic",
      .requested_qos = QoS::AtLeastOnce,
      .options = {}};
  client.async_subscribe(
      {subscribe_request},
      [&completion_mutex, &completion_condition, &subscribe_done](
          const std::optional<std::vector<ReasonCode>> &,
          const std::optional<AsyncOperationError> &) {
        {
          std::lock_guard<std::mutex> completion_guard(completion_mutex);
          subscribe_done = true;
        }
        completion_condition.notify_one();
      });
  REQUIRE(wait_until_true(completion_condition, completion_mutex, subscribe_done));
  CHECK(client.has_subscription("blocked/topic"));

  client.async_unsubscribe(
      {"blocked/topic"},
      [&completion_mutex, &completion_condition, &unsubscribe_done,
       &unsubscribe_error](
          const std::optional<std::vector<ReasonCode>> &result,
          const std::optional<AsyncOperationError> &operation_error) {
        CHECK_FALSE(result.has_value());
        REQUIRE(operation_error.has_value());
        {
          std::lock_guard<std::mutex> completion_guard(completion_mutex);
          unsubscribe_done = true;
          unsubscribe_error = operation_error;
        }
        completion_condition.notify_one();
      });

  REQUIRE(wait_until_true(completion_condition, completion_mutex, unsubscribe_done));
  REQUIRE(unsubscribe_error.has_value());
  CHECK(unsubscribe_error->category == ClientApiErrorCategory::Authorization);
  REQUIRE(unsubscribe_error->reason_code.has_value());
  CHECK(*unsubscribe_error->reason_code == ReasonCode::NotAuthorized);
  CHECK(client.has_subscription("blocked/topic"));
}

TEST_CASE(
    "async_client_publish_without_connect_maps_client_exception_to_async_error",
    "[client_api][async]") {
  AsyncClient client("async-client");

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool completion_called = false;
  std::optional<AsyncOperationError> publish_error;

  client.async_publish(
      make_message("topic/not-connected", QoS::AtLeastOnce),
      [&callback_mutex, &callback_condition, &completion_called,
       &publish_error](const std::optional<ReasonCode> &result,
                       const std::optional<AsyncOperationError> &operation_error) {
        CHECK_FALSE(result.has_value());
        REQUIRE(operation_error.has_value());
        {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          completion_called = true;
          publish_error = operation_error;
        }
        callback_condition.notify_one();
      });

  REQUIRE(wait_until_true(callback_condition, callback_mutex, completion_called));
  REQUIRE(publish_error.has_value());
  CHECK(publish_error->category == ClientApiErrorCategory::Protocol);
}

TEST_CASE("async_client_connect_runtime_error_maps_to_async_protocol_error",
          "[client_api][async]") {
  AsyncClient client("async-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate =
      [](const ConnectPacket &, uint32_t) -> ConnectionNegotiationResult {
    throw std::runtime_error("unexpected connect failure");
  };
  client.set_sync_callbacks(std::move(callbacks));

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool completion_called = false;
  std::optional<AsyncOperationError> connect_error;

  client.async_connect(
      make_connect_packet(),
      [&callback_mutex, &callback_condition, &completion_called,
       &connect_error](const std::optional<ConnectionNegotiationResult> &result,
                       const std::optional<AsyncOperationError> &operation_error) {
        CHECK_FALSE(result.has_value());
        REQUIRE(operation_error.has_value());
        {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          completion_called = true;
          connect_error = operation_error;
        }
        callback_condition.notify_one();
      });

  REQUIRE(wait_until_true(callback_condition, callback_mutex, completion_called));
  REQUIRE(connect_error.has_value());
  CHECK(connect_error->category == ClientApiErrorCategory::Unknown);
}

TEST_CASE("async_client_subscribe_without_connect_returns_async_error",
          "[client_api][async]") {
  AsyncClient client("async-client");

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool completion_called = false;
  std::optional<AsyncOperationError> subscribe_error;

  const AsyncSubscribeRequest subscribe_request{
      .topic_filter = "sensor/not-connected",
      .requested_qos = QoS::AtLeastOnce,
      .options = {}};
  client.async_subscribe(
      {subscribe_request},
      [&callback_mutex, &callback_condition, &completion_called,
       &subscribe_error](
          const std::optional<std::vector<ReasonCode>> &result,
          const std::optional<AsyncOperationError> &operation_error) {
        CHECK_FALSE(result.has_value());
        REQUIRE(operation_error.has_value());
        {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          completion_called = true;
          subscribe_error = operation_error;
        }
        callback_condition.notify_one();
      });

  REQUIRE(wait_until_true(callback_condition, callback_mutex, completion_called));
  REQUIRE(subscribe_error.has_value());
  CHECK(subscribe_error->category == ClientApiErrorCategory::Protocol);
}

TEST_CASE("async_client_unsubscribe_without_connect_returns_async_error",
          "[client_api][async]") {
  AsyncClient client("async-client");

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool completion_called = false;
  std::optional<AsyncOperationError> unsubscribe_error;

  client.async_unsubscribe(
      {"sensor/not-connected"},
      [&callback_mutex, &callback_condition, &completion_called,
       &unsubscribe_error](
          const std::optional<std::vector<ReasonCode>> &result,
          const std::optional<AsyncOperationError> &operation_error) {
        CHECK_FALSE(result.has_value());
        REQUIRE(operation_error.has_value());
        {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          completion_called = true;
          unsubscribe_error = operation_error;
        }
        callback_condition.notify_one();
      });

  REQUIRE(wait_until_true(callback_condition, callback_mutex, completion_called));
  REQUIRE(unsubscribe_error.has_value());
  CHECK(unsubscribe_error->category == ClientApiErrorCategory::Protocol);
}

TEST_CASE("async_client_disconnect_is_enqueued_and_prevents_later_publish",
          "[client_api][async]") {
  AsyncClient client("async-client");

  std::size_t publish_send_count = 0U;
  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_connect_result();
  };
  callbacks.send_publish = [&publish_send_count](const PublishPacket &) {
    ++publish_send_count;
  };
  client.set_sync_callbacks(std::move(callbacks));

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool connect_called = false;
  bool publish_called = false;
  std::optional<AsyncOperationError> publish_error;

  client.async_connect(
      make_connect_packet(),
      [&callback_mutex, &callback_condition,
       &connect_called](const std::optional<ConnectionNegotiationResult> &,
                        const std::optional<AsyncOperationError> &) {
        {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          connect_called = true;
        }
        callback_condition.notify_one();
      });
  REQUIRE(wait_until_true(callback_condition, callback_mutex, connect_called));

  client.async_disconnect(ReasonCode::Success);
  client.async_publish(
      make_message("topic/disconnected", QoS::AtMostOnce),
      [&callback_mutex, &callback_condition, &publish_called,
       &publish_error](const std::optional<ReasonCode> &result,
                       const std::optional<AsyncOperationError> &operation_error) {
        CHECK_FALSE(result.has_value());
        REQUIRE(operation_error.has_value());
        {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          publish_called = true;
          publish_error = operation_error;
        }
        callback_condition.notify_one();
      });

  REQUIRE(wait_until_true(callback_condition, callback_mutex, publish_called));
  REQUIRE(publish_error.has_value());
  CHECK(publish_error->category == ClientApiErrorCategory::Protocol);
  CHECK_FALSE(client.is_connected());
  CHECK(publish_send_count == 0U);
}

TEST_CASE("async_client_default_connect_without_callbacks_reports_error",
          "[client_api][async]") {
  AsyncClient client("async-client");

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool completion_called = false;
  std::optional<AsyncOperationError> connect_error;

  client.async_connect(
      [&callback_mutex, &callback_condition, &completion_called,
       &connect_error](const std::optional<ConnectionNegotiationResult> &result,
                       const std::optional<AsyncOperationError> &operation_error) {
        CHECK_FALSE(result.has_value());
        REQUIRE(operation_error.has_value());
        {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          completion_called = true;
          connect_error = operation_error;
        }
        callback_condition.notify_one();
      });

  REQUIRE(wait_until_true(callback_condition, callback_mutex, completion_called));
  REQUIRE(connect_error.has_value());
  CHECK(connect_error->category == ClientApiErrorCategory::Protocol);
}

TEST_CASE(
    "async_client_default_connect_completion_throwing_client_exception_is_converted_to_error_callback",
    "[client_api][async]") {
  AsyncClient client("async-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_connect_result();
  };
  client.set_sync_callbacks(std::move(callbacks));

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool completion_done = false;
  bool first_success_invocation = true;
  std::optional<AsyncOperationError> completion_error;

  client.async_connect(
      [&callback_mutex, &callback_condition, &completion_done,
       &first_success_invocation,
       &completion_error](
          const std::optional<ConnectionNegotiationResult> &result,
          const std::optional<AsyncOperationError> &operation_error) {
        if (result.has_value() && first_success_invocation) {
          first_success_invocation = false;
          throw ClientException(ClientError::Timeout,
                                "forced completion client exception");
        }

        if (operation_error.has_value()) {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          completion_done = true;
          completion_error = operation_error;
          callback_condition.notify_one();
        }
      });

  REQUIRE(wait_until_true(callback_condition, callback_mutex, completion_done));
  REQUIRE(completion_error.has_value());
  CHECK(completion_error->category == ClientApiErrorCategory::Timeout);
}

TEST_CASE(
    "async_client_default_connect_completion_throwing_std_exception_is_converted_to_error_callback",
    "[client_api][async]") {
  AsyncClient client("async-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_connect_result();
  };
  client.set_sync_callbacks(std::move(callbacks));

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool completion_done = false;
  bool first_success_invocation = true;
  std::optional<AsyncOperationError> completion_error;

  client.async_connect(
      [&callback_mutex, &callback_condition, &completion_done,
       &first_success_invocation,
       &completion_error](
          const std::optional<ConnectionNegotiationResult> &result,
          const std::optional<AsyncOperationError> &operation_error) {
        if (result.has_value() && first_success_invocation) {
          first_success_invocation = false;
          throw std::runtime_error("forced completion std exception");
        }

        if (operation_error.has_value()) {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          completion_done = true;
          completion_error = operation_error;
          callback_condition.notify_one();
        }
      });

  REQUIRE(wait_until_true(callback_condition, callback_mutex, completion_done));
  REQUIRE(completion_error.has_value());
  CHECK(completion_error->category == ClientApiErrorCategory::Unknown);
}

TEST_CASE(
    "async_client_publish_completion_throwing_client_exception_is_converted_to_error_callback",
    "[client_api][async]") {
  AsyncClient client("async-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_connect_result();
  };
  callbacks.send_publish = [](const PublishPacket &) {};
  client.set_sync_callbacks(std::move(callbacks));

  std::mutex connect_mutex;
  std::condition_variable connect_condition;
  bool connect_done = false;
  client.async_connect(
      make_connect_packet(),
      [&connect_mutex, &connect_condition,
       &connect_done](const std::optional<ConnectionNegotiationResult> &,
                      const std::optional<AsyncOperationError> &) {
        {
          std::lock_guard<std::mutex> connect_guard(connect_mutex);
          connect_done = true;
        }
        connect_condition.notify_one();
      });
  REQUIRE(wait_until_true(connect_condition, connect_mutex, connect_done));

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool completion_done = false;
  bool first_success_invocation = true;
  std::optional<AsyncOperationError> completion_error;

  client.async_publish(
      make_message("topic/throw/publish", QoS::AtMostOnce),
      [&callback_mutex, &callback_condition, &completion_done,
       &first_success_invocation,
       &completion_error](const std::optional<ReasonCode> &result,
                          const std::optional<AsyncOperationError> &operation_error) {
        if (result.has_value() && first_success_invocation) {
          first_success_invocation = false;
          throw ClientException(ClientError::Timeout,
                                "forced publish completion client exception");
        }

        if (operation_error.has_value()) {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          completion_done = true;
          completion_error = operation_error;
          callback_condition.notify_one();
        }
      });

  REQUIRE(wait_until_true(callback_condition, callback_mutex, completion_done));
  REQUIRE(completion_error.has_value());
  CHECK(completion_error->category == ClientApiErrorCategory::Timeout);
}

TEST_CASE(
    "async_client_subscribe_completion_throwing_std_exception_is_converted_to_error_callback",
    "[client_api][async]") {
  AsyncClient client("async-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_connect_result();
  };
  callbacks.send_subscribe = [](const SubscribePacket &) {};
  callbacks.wait_suback = [](uint16_t packet_identifier, uint32_t) {
    return SubackPacket{.packet_id = packet_identifier,
                        .properties = {},
                        .reason_codes = {ReasonCode::GrantedQoS1}};
  };
  client.set_sync_callbacks(std::move(callbacks));

  std::mutex connect_mutex;
  std::condition_variable connect_condition;
  bool connect_done = false;
  client.async_connect(
      make_connect_packet(),
      [&connect_mutex, &connect_condition,
       &connect_done](const std::optional<ConnectionNegotiationResult> &,
                      const std::optional<AsyncOperationError> &) {
        {
          std::lock_guard<std::mutex> connect_guard(connect_mutex);
          connect_done = true;
        }
        connect_condition.notify_one();
      });
  REQUIRE(wait_until_true(connect_condition, connect_mutex, connect_done));

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool completion_done = false;
  bool first_success_invocation = true;
  std::optional<AsyncOperationError> completion_error;

  const AsyncSubscribeRequest subscribe_request{
      .topic_filter = "topic/throw/subscribe",
      .requested_qos = QoS::AtLeastOnce,
      .options = {}};
  client.async_subscribe(
      {subscribe_request},
      [&callback_mutex, &callback_condition, &completion_done,
       &first_success_invocation,
       &completion_error](
          const std::optional<std::vector<ReasonCode>> &result,
          const std::optional<AsyncOperationError> &operation_error) {
        if (result.has_value() && first_success_invocation) {
          first_success_invocation = false;
          throw std::runtime_error(
              "forced subscribe completion std exception");
        }

        if (operation_error.has_value()) {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          completion_done = true;
          completion_error = operation_error;
          callback_condition.notify_one();
        }
      });

  REQUIRE(wait_until_true(callback_condition, callback_mutex, completion_done));
  REQUIRE(completion_error.has_value());
  CHECK(completion_error->category == ClientApiErrorCategory::Unknown);
}

TEST_CASE(
    "async_client_unsubscribe_completion_throwing_client_exception_is_converted_to_error_callback",
    "[client_api][async]") {
  AsyncClient client("async-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_connect_result();
  };
  callbacks.send_subscribe = [](const SubscribePacket &) {};
  callbacks.wait_suback = [](uint16_t packet_identifier, uint32_t) {
    return SubackPacket{.packet_id = packet_identifier,
                        .properties = {},
                        .reason_codes = {ReasonCode::GrantedQoS1}};
  };
  callbacks.send_unsubscribe = [](const UnsubscribePacket &) {};
  callbacks.wait_unsuback = [](uint16_t packet_identifier, uint32_t) {
    return UnsubackPacket{.packet_id = packet_identifier,
                          .properties = {},
                          .reason_codes = {ReasonCode::Success}};
  };
  client.set_sync_callbacks(std::move(callbacks));

  std::mutex completion_mutex;
  std::condition_variable completion_condition;
  bool connect_done = false;
  bool subscribe_done = false;
  client.async_connect(
      make_connect_packet(),
      [&completion_mutex, &completion_condition,
       &connect_done](const std::optional<ConnectionNegotiationResult> &,
                      const std::optional<AsyncOperationError> &) {
        {
          std::lock_guard<std::mutex> completion_guard(completion_mutex);
          connect_done = true;
        }
        completion_condition.notify_one();
      });
  REQUIRE(wait_until_true(completion_condition, completion_mutex, connect_done));

  const AsyncSubscribeRequest subscribe_request{
      .topic_filter = "topic/throw/unsubscribe",
      .requested_qos = QoS::AtLeastOnce,
      .options = {}};
  client.async_subscribe(
      {subscribe_request},
      [&completion_mutex, &completion_condition, &subscribe_done](
          const std::optional<std::vector<ReasonCode>> &,
          const std::optional<AsyncOperationError> &) {
        {
          std::lock_guard<std::mutex> completion_guard(completion_mutex);
          subscribe_done = true;
        }
        completion_condition.notify_one();
      });
  REQUIRE(wait_until_true(completion_condition, completion_mutex, subscribe_done));

  bool first_success_invocation = true;
  bool unsubscribe_done = false;
  std::optional<AsyncOperationError> unsubscribe_error;
  client.async_unsubscribe(
      {"topic/throw/unsubscribe"},
      [&completion_mutex, &completion_condition, &first_success_invocation,
       &unsubscribe_done,
       &unsubscribe_error](
          const std::optional<std::vector<ReasonCode>> &result,
          const std::optional<AsyncOperationError> &operation_error) {
        if (result.has_value() && first_success_invocation) {
          first_success_invocation = false;
          throw ClientException(ClientError::Timeout,
                                "forced unsubscribe completion client exception");
        }

        if (operation_error.has_value()) {
          std::lock_guard<std::mutex> completion_guard(completion_mutex);
          unsubscribe_done = true;
          unsubscribe_error = operation_error;
          completion_condition.notify_one();
        }
      });

  REQUIRE(
      wait_until_true(completion_condition, completion_mutex, unsubscribe_done));
  REQUIRE(unsubscribe_error.has_value());
  CHECK(unsubscribe_error->category == ClientApiErrorCategory::Timeout);
}

TEST_CASE(
    "async_client_completion_throwing_client_exception_is_converted_to_error_callback",
    "[client_api][async]") {
  AsyncClient client("async-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_connect_result();
  };
  client.set_sync_callbacks(std::move(callbacks));

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool completion_done = false;
  bool first_success_invocation = true;
  std::optional<AsyncOperationError> completion_error;

  client.async_connect(
      make_connect_packet(),
      [&callback_mutex, &callback_condition, &completion_done,
       &first_success_invocation,
       &completion_error](
          const std::optional<ConnectionNegotiationResult> &result,
          const std::optional<AsyncOperationError> &operation_error) {
        if (result.has_value() && first_success_invocation) {
          first_success_invocation = false;
          throw ClientException(ClientError::Timeout,
                                "forced completion client exception");
        }

        if (operation_error.has_value()) {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          completion_done = true;
          completion_error = operation_error;
          callback_condition.notify_one();
        }
      });

  REQUIRE(wait_until_true(callback_condition, callback_mutex, completion_done));
  REQUIRE(completion_error.has_value());
  CHECK(completion_error->category == ClientApiErrorCategory::Timeout);
}

TEST_CASE(
    "async_client_completion_throwing_std_exception_is_converted_to_error_callback",
    "[client_api][async]") {
  AsyncClient client("async-client");

  SyncClientCallbacks callbacks;
  callbacks.connect_and_negotiate = [](const ConnectPacket &, uint32_t) {
    return make_connect_result();
  };
  client.set_sync_callbacks(std::move(callbacks));

  std::mutex callback_mutex;
  std::condition_variable callback_condition;
  bool completion_done = false;
  bool first_success_invocation = true;
  std::optional<AsyncOperationError> completion_error;

  client.async_connect(
      make_connect_packet(),
      [&callback_mutex, &callback_condition, &completion_done,
       &first_success_invocation,
       &completion_error](
          const std::optional<ConnectionNegotiationResult> &result,
          const std::optional<AsyncOperationError> &operation_error) {
        if (result.has_value() && first_success_invocation) {
          first_success_invocation = false;
          throw std::runtime_error("forced completion std exception");
        }

        if (operation_error.has_value()) {
          std::lock_guard<std::mutex> callback_guard(callback_mutex);
          completion_done = true;
          completion_error = operation_error;
          callback_condition.notify_one();
        }
      });

  REQUIRE(wait_until_true(callback_condition, callback_mutex, completion_done));
  REQUIRE(completion_error.has_value());
  CHECK(completion_error->category == ClientApiErrorCategory::Unknown);
}

} // namespace mqtt
