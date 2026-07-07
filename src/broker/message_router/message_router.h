#pragma once

/**
 * @file message_router.h
 * @brief MessageRouter — top-level coordinator for MQTT 5.0 message dispatch
 *        (Module 12).
 */

#include <chrono>
#include <functional>
#include <cstddef>
#include <mutex>
#include <string_view>
#include <vector>

#include "connection/topic_alias_table.h"
#include "data_model/message/message.h"
#include "data_model/subscription/subscription.h"
#include "message_router/inbound_publish_processor.h"
#include "message_router/offline_queue.h"
#include "message_router/shared_subscription_dispatcher.h"
#include "message_router/subscriber_fanout.h"

namespace mqtt {

/**
 * @brief Forward declaration of StructuredTracer.
 */
class StructuredTracer;

/**
 * @brief Coordinates inbound PUBLISH processing and outbound delivery for all
 *        connected and offline subscribers (Module 12).
 *
 * Sequence performed by route():
 * 1. InboundPublishProcessor resolves the Topic Alias, checks authorization,
 *    stores retained messages, and returns the regular subscriber list (12.1).
 * 2. SharedSubscriptionDispatcher selects one target per matching shared
 *    subscription group (12.5).
 * 3. SubscriberFanout applies per-subscriber rules (QoS downgrade, No Local,
 *    Retain As Published, Subscription Identifier) for all candidates (12.2).
 * 4. MessageExpiryController checks and updates the Message Expiry Interval
 *    before each delivery (12.4).
 * 5. Online subscribers receive immediate delivery via the DeliverFn callback.
 *    Offline subscribers have their messages buffered in OfflineQueue (12.3).
 *
 * When a client reconnects, flush_offline_queue() drains the buffer, discards
 * any expired messages, and delivers the rest via DeliverFn (12.3.2).
 *
 * Thread safety: callback registration/access for
 * `on_offline_queue_changed_` is synchronized via `std::mutex`.
 * Other collaborator access relies on collaborator-internal synchronization.
 */
class MessageRouter {
public:
  /**
   * @brief Callback invoked to deliver a single message to an online client.
   *
   * @param client_id Identifier of the target client.
   * @param msg       Message ready for delivery.
   */
  using DeliverFn = std::function<void(std::string_view, const Message &)>;

  /**
   * @brief Callback that reports whether a client currently has an active
   *        connection.
   *
   * @param client_id Identifier of the client to test.
   * @return `true` when the client is online; `false` otherwise.
   */
  using IsOnlineFn = std::function<bool(std::string_view)>;

  /**
   * @brief Construct a MessageRouter.
   *
   * @param processor         Inbound publish pre-processor (12.1).
   * @param offline_queue     Buffer for offline subscriber messages (12.3).
   * @param shared_dispatcher Shared subscription round-robin dispatcher (12.5).
   * @param is_online         Predicate that tests whether a client is
   * connected.
   * @param deliver           Callback that forwards a message to an online
   * client.
    * @param structured_tracer Optional diagnostics tracer; nullptr disables
    * trace emission.
   */
  MessageRouter(InboundPublishProcessor &processor, OfflineQueue &offline_queue,
                SharedSubscriptionDispatcher &shared_dispatcher,
             IsOnlineFn is_online, DeliverFn deliver,
                StructuredTracer *structured_tracer = nullptr) noexcept;

  /**
   * @brief Route one inbound PUBLISH message to all matching subscribers
   *        (12.1–12.5).
   *
   * @param msg         Message to deliver; may be modified in-place
   *                    (alias resolution, retain storage).
   * @param client_id   Identifier of the publishing client.
   * @param username    Username of the publishing client; may be empty.
   * @param alias_table Topic alias table for the publishing connection.
   * @throws MessageRouterException(PublishNotAuthorized) when the client
   *         lacks publish permission.
   * @throws MessageRouterException(TopicAliasInvalid) on alias error.
   */
  bool route(Message &msg, std::string_view client_id,
             std::string_view username, TopicAliasTable &alias_table);

  /**
   * @brief Route a broker-internal message without caller-managed aliases.
   *
   * Uses an internal alias table with maximum 0 (aliases disabled).
   *
   * @param msg Message to route.
   * @param client_id Internal principal used for ACL checks.
   * @param username Optional username context; may be empty.
   */
  void route_internal(Message msg, std::string_view client_id,
                      std::string_view username = "");

  /**
   * @brief Deliver buffered messages to a client that has reconnected (12.3.2).
   *
   * Drains the offline queue for @p client_id.  Each message is passed
   * through MessageExpiryController; expired messages are discarded (12.4.2).
   * Remaining messages are forwarded via the DeliverFn callback.
   *
   * @param client_id Identifier of the reconnecting client.
   * @param now       Reference instant for expiry calculation; defaults to
   *                  steady_clock::now().
   */
  void flush_offline_queue(std::string_view client_id,
                           std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    /**
     * @brief Buffer already prepared outbound messages for an offline client.
     *
     * This helper is used by connection lifecycle code after draining pending
     * per-connection outbound queues.
     *
     * @param client_id Target client identifier.
     * @param messages Messages to enqueue in order.
     * @return Number of messages successfully enqueued.
     */
    [[nodiscard]] std::size_t buffer_offline_messages(
      std::string_view client_id, std::vector<Message> messages);

  /**
   * @brief Deliver retained messages for one subscription (25.1.2).
   *
   * Applies Retain Handling rules, per-subscription message adjustments,
   * and expiry checks before forwarding via DeliverFn.
   *
   * @param client_id Identifier of the subscribing client.
   * @param topic_filter Topic filter of the subscription.
   * @param subscription Stored subscription entry.
   * @param is_new_subscription True when the subscription was newly created.
   * @param now Reference instant for expiry calculation.
   */
  void deliver_retained(std::string_view client_id, std::string_view topic_filter,
                        const Subscription &subscription, bool is_new_subscription,
                        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

  /**
   * @brief Register a write-through callback invoked after every offline queue
   *        mutation (13.4).
   *
   * The callback is called after each enqueue in dispatch_item() and
   * buffer_offline_messages(), and after drain in flush_offline_queue().
   * It must be noexcept — any exception is silently swallowed.
   *
   * @param callback Callback to register; pass {} to clear.
   */
  void set_on_offline_queue_changed(std::function<void()> callback) noexcept;

private:
  /**
   * @brief Deliver one DeliveryItem: forward to online client or enqueue
   *        for offline client.
   *
   * @param item Delivery item (client_id + adjusted message).
   * @param now  Current time used for MessageExpiryController.
   */
  void dispatch_item(const DeliveryItem &item,
                     std::chrono::steady_clock::time_point now);

  /**
   * @brief Snapshot offline-queue-changed callback.
   * @return Callback copy.
   */
  [[nodiscard]] std::function<void()> snapshot_on_offline_queue_changed() const;
  /**
   * @brief Install offline-queue-changed callback.
   * @param callback Callback to install.
   */
  void set_on_offline_queue_changed_callback(std::function<void()> callback) noexcept;
  /**
   * @brief Emit offline-queue-changed callback when set.
   */
  void emit_on_offline_queue_changed() const noexcept;

  InboundPublishProcessor &processor_; ///< 12.1 — inbound pre-processing.
  OfflineQueue &offline_queue_;        ///< 12.3 — offline message buffer.
  SharedSubscriptionDispatcher
      &shared_dispatcher_; ///< 12.5 — shared subscription dispatch.
  IsOnlineFn is_online_;   ///< Online presence predicate.
  DeliverFn deliver_;      ///< Online delivery callback.
  StructuredTracer *const structured_tracer_;
  mutable std::mutex on_offline_queue_changed_callback_mutex_;
  std::function<void()> on_offline_queue_changed_{}; ///< Write-through callback (13.4).
};

} // namespace mqtt
