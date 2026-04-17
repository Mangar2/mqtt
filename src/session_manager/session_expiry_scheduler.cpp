#include "session_manager/session_expiry_scheduler.h"

namespace mqtt {

namespace {
    constexpr uint32_t k_never_expires = 0xFFFF'FFFFU;
} // namespace

void SessionExpiryScheduler::schedule(
    std::string_view client_id,
    std::chrono::steady_clock::time_point disconnect_time,
    uint32_t expiry_interval) {
    timers_.insert_or_assign(std::string(client_id),
                              TimerEntry{disconnect_time, expiry_interval});
}

void SessionExpiryScheduler::cancel(std::string_view client_id) {
    timers_.erase(std::string(client_id));
}

std::vector<std::string> SessionExpiryScheduler::collect_expired(
    std::chrono::steady_clock::time_point now) const {
    std::vector<std::string> result;

    for (const auto &[cid, entry] : timers_) {
        if (entry.expiry_interval == k_never_expires) {
            continue;
        }
        if (entry.expiry_interval == 0U) {
            result.push_back(cid);
            continue;
        }
        const auto deadline =
            entry.disconnect_time + std::chrono::seconds(entry.expiry_interval);
        if (now >= deadline) {
            result.push_back(cid);
        }
    }

    return result;
}

std::size_t SessionExpiryScheduler::size() const noexcept {
    return timers_.size();
}

} // namespace mqtt
