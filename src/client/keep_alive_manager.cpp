#include "client/keep_alive_manager.h"

#include "codec/packet/control_codec.h"

namespace mqtt {

KeepAliveManager::KeepAliveManager(
    uint16_t keep_alive_seconds, std::chrono::milliseconds pingresp_timeout)
    : keep_alive_interval_(std::chrono::seconds(keep_alive_seconds)),
      pingresp_timeout_(pingresp_timeout), last_activity_(Clock::now()),
      ping_deadline_(Clock::time_point::min()), enabled_(keep_alive_seconds > 0U),
      ping_outstanding_(false) {
  if (pingresp_timeout_ <= std::chrono::milliseconds::zero()) {
    pingresp_timeout_ = keep_alive_interval_;
  }
}

void KeepAliveManager::note_activity(Clock::time_point now) noexcept {
  if (!enabled_) {
    return;
  }
  ping_outstanding_ = false;
  last_activity_ = now;
}

void KeepAliveManager::on_pingresp(Clock::time_point now) noexcept {
  note_activity(now);
}

KeepAliveAction KeepAliveManager::poll(Clock::time_point now) noexcept {
  if (!enabled_) {
    return KeepAliveAction::None;
  }

  if (ping_outstanding_) {
    if (now >= ping_deadline_) {
      return KeepAliveAction::Timeout;
    }
    return KeepAliveAction::None;
  }

  if (now - last_activity_ >= keep_alive_interval_) {
    ping_outstanding_ = true;
    ping_deadline_ = now + pingresp_timeout_;
    return KeepAliveAction::SendPingreq;
  }

  return KeepAliveAction::None;
}

bool KeepAliveManager::is_enabled() const noexcept { return enabled_; }

bool KeepAliveManager::awaiting_pingresp() const noexcept {
  return ping_outstanding_;
}

WriteBuffer KeepAliveManager::encode_pingreq_frame() {
  WriteBuffer frame;
  encode_pingreq(frame);
  return frame;
}

} // namespace mqtt
