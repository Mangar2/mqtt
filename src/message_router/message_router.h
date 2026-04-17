#pragma once

/**
 * @file message_router.h
 * @brief MessageRouter — top-level coordinator for MQTT 5.0 message dispatch
 *        (Module 12).
 */

#include <chrono>
#include <functional>
#include <string_view>

#include "authz/acl_engine.h"
#include "connection/topic_alias_table.h"
#include "data_model/message/message.h"
#include "message_router/inbound_publish_processor.h"
#include "message_router/message_expiry_controller.h"
#include "message_router/offline_queue.h"
#include "message_router/shared_subscription_dispatcher.h"
#include "message_router/subscriber_fanout.h"

namespace mqtt {

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
 * Thread safety: none — external synchronisation required.
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
   */
  MessageRouter(InboundPublishProcessor &processor, OfflineQueue &offline_queue,
                SharedSubscriptionDispatcher &shared_dispatcher,
                IsOnlineFn is_online, DeliverFn deliver);

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
  void route(Message &msg, std::string_view client_id,
             std::string_view username, TopicAliasTable &alias_table);

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
                           std::chrono::steady_clock::time_point now =
                               std::chrono::steady_clock::now());

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

  InboundPublishProcessor &processor_; ///< 12.1 — inbound pre-processing.
  OfflineQueue &offline_queue_;        ///< 12.3 — offline message buffer.
  SharedSubscriptionDispatcher
      &shared_dispatcher_; ///< 12.5 — shared subscription dispatch.
  IsOnlineFn is_online_;   ///< Online presence predicate.
  DeliverFn deliver_;      ///< Online delivery callback.
};

} // namespace mqtt
