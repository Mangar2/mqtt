#pragma once

/**
 * @file async_client.h
 * @brief Non-blocking MQTT client facade with callback completion (Step 24).
 */

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "client/client_error.h"
#include "client/subscription_manager.h"
#include "client_api/sync_client.h"
#include "data_model/message/message.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/packet/control_packets.h"

namespace mqtt {

/**
 * @brief Callback-friendly error payload used by AsyncClient completion handlers.
 */
struct AsyncOperationError {
  ClientError error_code{ClientError::ProtocolError};
  std::string message;
  std::optional<ReasonCode> reason_code;
};

/**
 * @brief Subscribe request for AsyncClient (message handler is registered globally).
 */
struct AsyncSubscribeRequest {
  std::string topic_filter;
  QoS requested_qos{QoS::AtMostOnce};
  SubscriptionOptions options{};
};

/**
 * @brief Non-blocking public MQTT client interface.
 *
 * AsyncClient enqueues API operations and executes them on one internal dispatch
 * thread. Completion callbacks and inbound message callbacks are also invoked on
 * this same dispatch thread.
 */
class AsyncClient {
public:
  using MessageHandler = std::function<void(const PublishPacket &)>;

  using ConnectCompletion = std::function<void(
      const std::optional<ConnectionNegotiationResult> &,
      const std::optional<AsyncOperationError> &)>;
  using PublishCompletion =
      std::function<void(const std::optional<ReasonCode> &,
                         const std::optional<AsyncOperationError> &)>;
  using SubscribeCompletion =
      std::function<void(const std::optional<std::vector<ReasonCode>> &,
                         const std::optional<AsyncOperationError> &)>;
  using UnsubscribeCompletion =
      std::function<void(const std::optional<std::vector<ReasonCode>> &,
                         const std::optional<AsyncOperationError> &)>;

  /**
   * @brief Construct asynchronous client facade.
   * @param client_id Logical client identifier.
   * @param reconnect_backoff Backoff settings passed to wrapped SyncClient.
   */
  explicit AsyncClient(
      std::string client_id,
      ReconnectBackoffPolicy reconnect_backoff = ReconnectBackoffPolicy{});

  /**
   * @brief Construct asynchronous client facade from unified configuration.
   * @param client_config Public client configuration.
   */
  explicit AsyncClient(ClientConfig client_config);

  /**
   * @brief Join internal dispatch thread and stop processing queued operations.
   */
  ~AsyncClient();

  AsyncClient(const AsyncClient &) = delete;
  AsyncClient &operator=(const AsyncClient &) = delete;

  /**
   * @brief Install transport integration callbacks used by wrapped SyncClient.
   */
  void set_sync_callbacks(SyncClientCallbacks callbacks);

  /**
   * @brief Install inbound message handler.
   *
   * The handler is invoked on the internal dispatch thread.
   */
  void set_message_handler(MessageHandler message_handler);

  /**
   * @brief Enqueue non-blocking connect operation.
   * @param connect_packet CONNECT packet model.
   * @param completion Completion callback.
   * @param timeout_ms Operation timeout in milliseconds.
   */
  void async_connect(const ConnectPacket &connect_packet,
                     ConnectCompletion completion,
                     uint32_t timeout_ms = 0U);

  /**
   * @brief Enqueue non-blocking connect operation using configured defaults.
   * @param completion Completion callback.
   */
  void async_connect(ConnectCompletion completion);

  /**
   * @brief Enqueue non-blocking publish operation.
   * @param message Outbound message model.
   * @param completion Completion callback.
   * @param timeout_ms Operation timeout in milliseconds.
   */
  void async_publish(const Message &message, PublishCompletion completion,
                     uint32_t timeout_ms = 0U);

  /**
   * @brief Enqueue non-blocking subscribe operation.
   * @param requests Requested filters and options.
   * @param completion Completion callback.
   * @param timeout_ms Operation timeout in milliseconds.
   */
  void async_subscribe(const std::vector<AsyncSubscribeRequest> &requests,
                       SubscribeCompletion completion,
                       uint32_t timeout_ms = 0U);

  /**
   * @brief Enqueue non-blocking unsubscribe operation.
   * @param topic_filters Requested filters to remove.
   * @param completion Completion callback.
   * @param timeout_ms Operation timeout in milliseconds.
   */
  void async_unsubscribe(const std::vector<std::string> &topic_filters,
                         UnsubscribeCompletion completion,
                         uint32_t timeout_ms = 0U);

  /**
   * @brief Enqueue non-blocking disconnect operation.
   * @param reason_code Disconnect reason code.
   */
  void async_disconnect(ReasonCode reason_code = ReasonCode::Success);

  /**
   * @brief Enqueue inbound publish dispatch to run on dispatch thread.
   */
  void on_inbound_publish(const PublishPacket &publish_packet);

  /**
   * @brief Return current connection state.
   */
  [[nodiscard]] bool is_connected() const;

  /**
   * @brief Return whether a topic filter is active.
   */
  [[nodiscard]] bool has_subscription(std::string_view topic_filter) const;

  /**
   * @brief Return a copy of the current client configuration.
   */
  [[nodiscard]] ClientConfig client_config() const;

private:
  using DispatchTask = std::function<void()>;

  void enqueue_task(DispatchTask task);
  void run_dispatch_loop();
  void invoke_message_handler(const PublishPacket &publish_packet);

  [[nodiscard]] static AsyncOperationError
  to_async_error(const ClientException &exception);
  [[nodiscard]] static AsyncOperationError
  to_async_error(const std::exception &exception);

  mutable std::mutex state_mutex_;
  mutable std::mutex message_handler_mutex_;
  SyncClient sync_client_;
  MessageHandler message_handler_;

  std::mutex queue_mutex_;
  std::condition_variable queue_condition_;
  std::deque<DispatchTask> dispatch_queue_;
  bool stop_requested_{false};
  std::thread dispatch_thread_;
};

} // namespace mqtt
