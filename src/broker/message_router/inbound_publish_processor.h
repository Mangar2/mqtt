#pragma once

/**
 * @file inbound_publish_processor.h
 * @brief InboundPublishProcessor — validates and pre-processes incoming PUBLISH
 *        messages before subscriber fanout (Module 12.1).
 */

#include <functional>
#include <mutex>
#include <string_view>
#include <vector>

#include "authz/acl_engine.h"
#include "connection/topic_alias_table.h"
#include "data_model/message/message.h"
#include "store/retained_message_store.h"
#include "store/subscription_store.h"
#include "topic/topic_matcher.h"

namespace mqtt {

/**
 * @brief Pre-processes inbound PUBLISH messages (Module 12.1).
 *
 * Steps performed in order on every call to process():
 * 1. **Topic Alias resolution** (12.1.1): if the message carries a
 *    TopicAlias property, the alias is registered or resolved via the
 *    connection's TopicAliasTable and `msg.topic` is rewritten in-place.
 *    The TopicAlias property is removed from the outgoing message.
 * 2. **Publish authorization** (12.1.2): the AclEngine is consulted; throws
 *    MessageRouterException(PublishNotAuthorized) on denial.
 * 3. **Retained message storage** (12.1.3): when `msg.retain` is set, the
 *    message is forwarded to RetainedMessageStore.
 * 4. **Subscriber lookup** (12.1.4): SubscriptionStore returns all clients
 *    whose topic filter matches the resolved `msg.topic`.
 *
 * Thread safety: collaborator access relies on collaborator-internal locking.
 * Callback registration/access is synchronized via `std::mutex`.
 */
class InboundPublishProcessor {
public:
  /**
   * @brief Construct an InboundPublishProcessor.
   *
   * @param acl           ACL engine for publish authorization (9.1).
   * @param retained      Retained message store (4.2).
   * @param subscriptions Subscription store (4.1).
   */
  InboundPublishProcessor(AclEngine &acl, RetainedMessageStore &retained,
                          SubscriptionStore &subscriptions);

  /**
   * @brief Pre-process one inbound PUBLISH message (12.1.1–12.1.4).
   *
   * @param msg         Message to process.  On return, `msg.topic` is fully
   *                    resolved and the TopicAlias property is removed.
   * @param client_id   Identifier of the publishing client.
   * @param username    Username of the publishing client; may be empty.
   * @param alias_table Topic alias table for the publishing connection
   * (12.1.1).
   * @return Vector of matching subscribers; may be empty when none match.
   * @throws MessageRouterException(PublishNotAuthorized) when the client is
   *         denied publish access to the resolved topic.
   * @throws MessageRouterException(TopicAliasInvalid) when the TopicAlias
   *         property value is 0, out of range, or references an unregistered
   *         alias while the topic string is empty.
   */
  [[nodiscard]] std::vector<MatchResult> process(Message &msg,
                                                 std::string_view client_id,
                                                 std::string_view username,
                                                 TopicAliasTable &alias_table);

  /**
   * @brief Retrieve retained messages matching a subscription topic filter.
   *
   * @param topic_filter Topic filter expression.
    * @return Matching retained records (message + store timestamp).
   */
  [[nodiscard]] std::vector<RetainedMessageRecord> retained_for_filter(std::string_view topic_filter) const;

  /**
   * @brief Register a write-through callback invoked after every retained
   *        message store mutation (13.4).
   *
   * Called whenever a retained message is stored (added or replaced) via
   * process(). The callback must be noexcept — any exception is silently
   * swallowed.
   *
   * @param callback Callback to register; pass {} to clear.
   */
  void set_on_retained_changed(std::function<void()> callback) noexcept;

private:
  /**
   * @brief Resolve the TopicAlias property in msg in-place (12.1.1).
   *
   * @param msg         Message whose TopicAlias property is resolved.
   * @param alias_table Per-connection alias table.
   * @throws MessageRouterException(TopicAliasInvalid) on alias error.
   */
  static void resolve_topic_alias(Message &msg, TopicAliasTable &alias_table);

  /**
   * @brief Snapshot retained-change callback.
   * @return Callback copy.
   */
  [[nodiscard]] std::function<void()> snapshot_on_retained_changed() const;
  /**
   * @brief Install retained-change callback.
   * @param callback Callback to install.
   */
  void set_on_retained_changed_callback(std::function<void()> callback) noexcept;
  /**
   * @brief Emit retained-change callback when present.
   */
  void emit_on_retained_changed() const noexcept;

  AclEngine &acl_;                                ///< ACL engine for publish authorization.
  mutable std::mutex on_retained_change_callback_mutex_;
  RetainedMessageStore &retained_;               ///< Retained message store.
  SubscriptionStore &subscriptions_;             ///< Subscription store.
  std::function<void()> on_retained_changed_{};  ///< Write-through callback (13.4).
};

} // namespace mqtt
