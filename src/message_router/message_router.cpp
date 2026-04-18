#include "message_router/message_router.h"

#include <chrono>

#include "data_model/subscription/retain_handling.h"
#include "message_router/message_expiry_controller.h"


namespace mqtt {

MessageRouter::MessageRouter(InboundPublishProcessor &processor,
                             OfflineQueue &offline_queue,
                             SharedSubscriptionDispatcher &shared_dispatcher,
                             IsOnlineFn is_online, DeliverFn deliver)
    : processor_(processor), offline_queue_(offline_queue),
      shared_dispatcher_(shared_dispatcher), is_online_(std::move(is_online)),
      deliver_(std::move(deliver)) {}

void MessageRouter::route(Message &msg, std::string_view client_id,
                          std::string_view username,
                          TopicAliasTable &alias_table) {
  const auto now = std::chrono::steady_clock::now();

  // 12.1 — Pre-process inbound message; retrieve regular subscribers.
  std::vector<MatchResult> regular_subscribers =
      processor_.process(msg, client_id, username, alias_table);

  // 12.5 — Select one target per matching shared subscription group.
  std::vector<MatchResult> shared_targets =
      shared_dispatcher_.select_next_for_topic(msg.topic.value);

  // 12.2 — Apply per-subscription rules for regular subscribers.
  auto regular_items =
      SubscriberFanout::prepare(msg, regular_subscribers, client_id);

  // 12.2 — Apply per-subscription rules for shared subscription targets.
  auto shared_items = SubscriberFanout::prepare(msg, shared_targets, client_id);

  // 12.4 + online/offline dispatch for regular subscribers.
  for (const auto &item : regular_items) {
    dispatch_item(item, now);
  }

  // 12.4 + online/offline dispatch for shared subscription targets.
  for (const auto &item : shared_items) {
    dispatch_item(item, now);
  }
}

void MessageRouter::dispatch_item(const DeliveryItem &item,
                                  std::chrono::steady_clock::time_point now) {
  Message msg_copy = item.message;

  // 12.4.2 — Discard expired messages before delivery.
  // For immediate delivery the elapsed time is ~0, but we still update the
  // MessageExpiryInterval property so the outbound PUBLISH carries the
  // reduced remaining lifetime (12.4.3).
  if (!MessageExpiryController::update_expiry(msg_copy, now, now)) {
    return; // Already expired.
  }

  if (is_online_(item.client_id)) {
    deliver_(item.client_id, msg_copy);
  } else {
    offline_queue_.enqueue(item.client_id, msg_copy);
  }
}

void MessageRouter::flush_offline_queue(
    std::string_view client_id, std::chrono::steady_clock::time_point now) {

  std::vector<QueuedMessage> queued = offline_queue_.drain(client_id);

  for (auto &queued_msg : queued) {
    // 12.4 — Discard expired, update remaining interval for valid messages.
    if (!MessageExpiryController::update_expiry(queued_msg.message,
                                                queued_msg.enqueue_time, now)) {
      continue; // Expired — skip delivery.
    }

    deliver_(client_id, queued_msg.message);
  }
}

void MessageRouter::deliver_retained(
    std::string_view client_id, std::string_view topic_filter,
    const Subscription &subscription, bool is_new_subscription,
    std::chrono::steady_clock::time_point now) {
  if (!is_online_(client_id)) {
    return;
  }

  if (subscription.options.retain_handling == RetainHandling::Never) {
    return;
  }

  if (subscription.options.retain_handling == RetainHandling::SendIfNew &&
      !is_new_subscription) {
    return;
  }

  std::vector<Message> retained_messages =
      processor_.retained_for_filter(topic_filter);
  for (const Message &retained_message : retained_messages) {
    Message outbound = SubscriberFanout::apply_subscription_rules(
        retained_message, subscription);

    if (!MessageExpiryController::update_expiry(outbound, now, now)) {
      continue;
    }

    deliver_(client_id, outbound);
  }
}

} // namespace mqtt
