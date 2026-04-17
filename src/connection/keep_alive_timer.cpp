#include "connection/keep_alive_timer.h"

namespace mqtt {

namespace {

constexpr double k_keep_alive_factor = 1.5;

} // namespace

KeepAliveTimer::KeepAliveTimer(uint16_t keep_alive_seconds) noexcept
    : interval_ms_(static_cast<long long>(keep_alive_seconds *
                                          k_keep_alive_factor * 1000.0)),
      deadline_(Clock::now() + interval_ms_),
      enabled_(keep_alive_seconds > 0U) {}

void KeepAliveTimer::reset() noexcept {
  if (!enabled_) {
    return;
  }
  deadline_ = Clock::now() + interval_ms_;
}

bool KeepAliveTimer::is_expired() const noexcept {
  if (!enabled_) {
    return false;
  }
  return Clock::now() > deadline_;
}

bool KeepAliveTimer::is_enabled() const noexcept { return enabled_; }

} // namespace mqtt
