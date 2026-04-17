#include "will_manager/will_publisher.h"

namespace mqtt {

WillPublisher::WillPublisher(WillStore &will_store, WillDelayTimer &delay_timer,
                             PublishCallback publish_fn)
    : will_store_(will_store), delay_timer_(delay_timer),
      publish_fn_(std::move(publish_fn)) {}

void WillPublisher::on_connect(std::string_view client_id,
                               const WillMessage &will) {
  will_store_.store(client_id, will);
}

void WillPublisher::on_reconnect(std::string_view client_id) {
  delay_timer_.cancel(client_id);
}

void WillPublisher::on_disconnect(std::string_view client_id, ReasonCode reason,
                                  std::chrono::steady_clock::time_point now) {
  if (reason == ReasonCode::Success) {
    // Normal disconnection (Reason 0x00) — suppress the will (11.3.2).
    will_store_.remove(client_id);
    delay_timer_.cancel(client_id);
    return;
  }
  // Reason 0x04 or any other non-zero reason — arm the delay timer (11.3.3).
  arm_timer(client_id, now);
}

void WillPublisher::on_connection_lost(
    std::string_view client_id, std::chrono::steady_clock::time_point now) {
  arm_timer(client_id, now);
}

void WillPublisher::on_session_expired(std::string_view client_id) {
  const auto will = will_store_.load(client_id);
  if (!will.has_value()) {
    return;
  }
  publish_fn_(*will);
  will_store_.remove(client_id);
  delay_timer_.cancel(client_id);
}

void WillPublisher::publish_due(std::chrono::steady_clock::time_point now) {
  const auto due = delay_timer_.collect_due(now);
  for (const auto &cid : due) {
    const auto will = will_store_.load(cid);
    if (will.has_value()) {
      publish_fn_(*will);
      will_store_.remove(cid);
    }
    delay_timer_.cancel(cid);
  }
}

// ─── private ─────────────────────────────────────────────────────────────────

void WillPublisher::arm_timer(std::string_view client_id,
                              std::chrono::steady_clock::time_point now) {
  const auto will = will_store_.load(client_id);
  if (!will.has_value()) {
    return;
  }
  if (will->delay_interval == 0U) {
    // Publish immediately when no delay is configured.
    publish_fn_(*will);
    will_store_.remove(client_id);
    return;
  }
  delay_timer_.schedule(client_id, now, will->delay_interval);
}

} // namespace mqtt
