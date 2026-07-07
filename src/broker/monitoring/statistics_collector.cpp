/**
 * @file statistics_collector.cpp
 * @brief StatisticsCollector implementation (Module 16.1).
 */

#include "monitoring/statistics_collector.h"

namespace mqtt {

StatisticsCollector::StatisticsCollector(
    const SubscriptionStore &sub_store,
    const RetainedMessageStore &retained_store)
    : sub_store_(sub_store), retained_store_(retained_store) {}

void StatisticsCollector::on_client_connected() noexcept {
  connected_clients_.fetch_add(1U, std::memory_order_relaxed);
}

void StatisticsCollector::on_client_disconnected() noexcept {
  connected_clients_.fetch_sub(1U, std::memory_order_relaxed);
}

void StatisticsCollector::on_message_inbound() noexcept {
  messages_inbound_.fetch_add(1U, std::memory_order_relaxed);
}

void StatisticsCollector::on_message_outbound() noexcept {
  messages_outbound_.fetch_add(1U, std::memory_order_relaxed);
}

StatisticsSnapshot StatisticsCollector::snapshot() const noexcept {
  const auto now = std::chrono::steady_clock::now();
  const auto uptime =
      std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);

  return StatisticsSnapshot{
      .connected_clients = connected_clients_.load(std::memory_order_relaxed),
      .messages_inbound = messages_inbound_.load(std::memory_order_relaxed),
      .messages_outbound = messages_outbound_.load(std::memory_order_relaxed),
      .active_subscriptions = sub_store_.size(),
      .retained_messages = retained_store_.size(),
      .uptime = uptime,
  };
}

} // namespace mqtt
