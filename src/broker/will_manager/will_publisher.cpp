#include "will_manager/will_publisher.h"

#include <vector>

namespace mqtt {

WillPublisher::WillPublisher(WillStore &will_store, WillDelayTimer &delay_timer,
                             PublishCallback publish_fn)
    : will_store_(will_store), delay_timer_(delay_timer),
      publish_fn_(std::move(publish_fn)) {}

void WillPublisher::on_connect(std::string_view client_id,
                               const WillMessage &will) {
  std::lock_guard<std::mutex> lock(mutex_);
  will_store_.store(client_id, will);
}

void WillPublisher::on_reconnect(std::string_view client_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  delay_timer_.cancel(client_id);
}

void WillPublisher::on_disconnect(std::string_view client_id, ReasonCode reason,
                                  std::chrono::steady_clock::time_point now) {
  std::optional<WillMessage> immediate_publish;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (reason == ReasonCode::Success) {
      // Normal disconnection (Reason 0x00) — suppress the will (11.3.2).
      will_store_.remove(client_id);
      delay_timer_.cancel(client_id);
      return;
    }
    // Reason 0x04 or any other non-zero reason — arm the delay timer (11.3.3).
    immediate_publish = arm_timer(client_id, now);
  }
  if (immediate_publish.has_value()) {
    publish_fn_(*immediate_publish);
  }
}

void WillPublisher::on_connection_lost(
    std::string_view client_id, std::chrono::steady_clock::time_point now) {
  std::optional<WillMessage> immediate_publish;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    immediate_publish = arm_timer(client_id, now);
  }
  if (immediate_publish.has_value()) {
    publish_fn_(*immediate_publish);
  }
}

void WillPublisher::on_session_expired(std::string_view client_id) {
  std::optional<WillMessage> will_to_publish;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto will = will_store_.load(client_id);
    if (!will.has_value()) {
      return;
    }
    will_to_publish = *will;
    will_store_.remove(client_id);
    delay_timer_.cancel(client_id);
  }
  publish_fn_(*will_to_publish);
}

void WillPublisher::publish_due(std::chrono::steady_clock::time_point now) {
  std::vector<WillMessage> will_batch;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto due = delay_timer_.collect_due(now);
    for (const auto &cid : due) {
      const auto will = will_store_.load(cid);
      if (will.has_value()) {
        will_batch.push_back(*will);
        will_store_.remove(cid);
      }
      delay_timer_.cancel(cid);
    }
  }

  for (const auto &will : will_batch) {
    publish_fn_(will);
  }
}

//  private

std::optional<WillMessage>
WillPublisher::arm_timer(std::string_view client_id,
                         std::chrono::steady_clock::time_point now) {
  const auto will = will_store_.load(client_id);
  if (!will.has_value()) {
    return std::nullopt;
  }
  if (will->delay_interval == 0U) {
    // Publish immediately when no delay is configured.
    WillMessage immediate_will = *will;
    will_store_.remove(client_id);
    return immediate_will;
  }
  delay_timer_.schedule(client_id, now, will->delay_interval);
  return std::nullopt;
}

} // namespace mqtt
