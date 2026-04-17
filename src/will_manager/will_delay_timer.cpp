#include "will_manager/will_delay_timer.h"

#include <string>

namespace mqtt {

void WillDelayTimer::schedule(
    std::string_view client_id,
    std::chrono::steady_clock::time_point disconnect_time,
    uint32_t delay_seconds) {
  timers_.insert_or_assign(std::string(client_id),
                           TimerEntry{disconnect_time, delay_seconds});
}

void WillDelayTimer::cancel(std::string_view client_id) {
  timers_.erase(std::string(client_id));
}

std::vector<std::string>
WillDelayTimer::collect_due(std::chrono::steady_clock::time_point now) const {
  std::vector<std::string> due;
  for (const auto &[cid, entry] : timers_) {
    const auto deadline =
        entry.disconnect_time + std::chrono::seconds(entry.delay_seconds);
    if (deadline <= now) {
      due.push_back(cid);
    }
  }
  return due;
}

std::size_t WillDelayTimer::size() const noexcept { return timers_.size(); }

} // namespace mqtt
