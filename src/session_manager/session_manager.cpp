#include "session_manager/session_manager.h"

#include <format>

#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/session/session_state.h"
#include "session_manager/session_manager_error.h"

namespace mqtt {

namespace {

/// Extract SessionExpiryInterval property value from a property list.
std::optional<uint32_t>
find_session_expiry(const std::vector<Property> &props) {
    for (const auto &prop : props) {
        if (prop.id == PropertyId::SessionExpiryInterval) {
            return std::get<FourByteInteger>(prop.value);
        }
    }
    return std::nullopt;
}

} // namespace

SessionManager::SessionManager(SessionStore &session_store,
                                SubscriptionStore &subscription_store,
                                InflightStore &inflight_store,
                                SessionTakeoverHandler &takeover_handler,
                                SessionExpiryScheduler &expiry_scheduler)
    : session_store_(session_store),
      subscription_store_(subscription_store),
      inflight_store_(inflight_store),
      takeover_handler_(takeover_handler),
      expiry_scheduler_(expiry_scheduler) {}

SessionOpenResult SessionManager::handle_connect(
    const ConnectPacket &connect, std::function<void()> close_callback) {
    // 1. Validate client_id.
    if (connect.client_id.value.empty()) {
        throw SessionManagerException(SessionManagerError::InvalidClientId,
                                      "CONNECT client_id must not be empty");
    }

    const std::string_view cid = connect.client_id.value;

    // 2. Session takeover (10.2).
    const bool takeover = takeover_handler_.takeover_if_exists(cid);

    SessionOpenResult result{.session_present = false,
                             .takeover_occurred = takeover};

    // 3 & 4. Session lifecycle based on clean_start flag.
    if (connect.clean_start) {
        // 10.1.1: discard any existing session and start fresh.
        remove_session_data(cid);

        const uint32_t expiry =
            find_session_expiry(connect.properties).value_or(0U);
        SessionState new_session;
        new_session.client_id.value = std::string(cid);
        new_session.session_expiry_interval = expiry;
        session_store_.create(new_session);
    } else {
        // 10.1.2: try to resume an existing session.
        const auto existing = session_store_.load(cid);
        if (existing.has_value()) {
            // Resume — cancel any pending expiry timer (10.3.2).
            expiry_scheduler_.cancel(cid);
            result.session_present = true; // 10.1.3

            // Apply connect-time expiry override when present.
            const auto override_expiry =
                find_session_expiry(connect.properties);
            if (override_expiry.has_value()) {
                SessionState updated = *existing;
                updated.session_expiry_interval = *override_expiry;
                session_store_.remove(cid);
                session_store_.create(updated);
            }
        } else {
            // No prior session — create a fresh one.
            const uint32_t expiry =
                find_session_expiry(connect.properties).value_or(0U);
            SessionState new_session;
            new_session.client_id.value = std::string(cid);
            new_session.session_expiry_interval = expiry;
            session_store_.create(new_session);
        }
    }

    // 5. Register the connection for future takeover.
    takeover_handler_.register_connection(cid, std::move(close_callback));

    return result;
}

void SessionManager::handle_disconnect(
    std::string_view client_id,
    std::optional<uint32_t> expiry_override,
    std::chrono::steady_clock::time_point now) {
    // 1. Unregister from takeover tracking.
    takeover_handler_.unregister_connection(client_id);

    // 2. Compute effective expiry interval.
    const auto stored = session_store_.load(client_id);
    const uint32_t stored_expiry =
        stored.has_value() ? stored->session_expiry_interval : 0U;
    const uint32_t effective_expiry =
        expiry_override.has_value() ? *expiry_override : stored_expiry;

    // 3. If expiry == 0: discard session immediately (10.1.4).
    if (effective_expiry == 0U) {
        remove_session_data(client_id);
        return;
    }

    // 4. Persist session — record disconnect time and schedule expiry (10.3.1).
    session_store_.mark_disconnected(client_id, now);
    expiry_scheduler_.schedule(client_id, now, effective_expiry);
}

std::vector<std::string> SessionManager::cleanup_expired(
    std::chrono::steady_clock::time_point now) {
    const std::vector<std::string> expired = expiry_scheduler_.collect_expired(now);

    for (const auto &cid : expired) {
        remove_session_data(cid);
        expiry_scheduler_.cancel(cid);
    }

    return expired;
}

void SessionManager::remove_session_data(std::string_view client_id) {
    // Remove all inflight entries for the session.
    const auto entries = inflight_store_.entries_for(client_id);
    for (const auto &entry : entries) {
        inflight_store_.remove(client_id, entry.packet_id, entry.direction);
    }

    // Remove all subscriptions.
    subscription_store_.remove_session(client_id);

    // Remove the session record.
    session_store_.remove(client_id);
}

} // namespace mqtt
