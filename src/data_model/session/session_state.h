#pragma once

/**
 * @file session_state.h
 * @brief MQTT 5.0 Session State structure (Module 1.7.1).
 */

#include <cstdint>
#include <vector>

#include "data_model/subscription/subscription.h"
#include "data_model/types/utf8_string.h"

namespace mqtt {

/**
 * @brief Persistent broker-side state for a single client session (Module 1.7.1).
 *
 * Owned by the Session Store (Module 4.3) and managed by the Session Manager
 * (Module 10). In-flight entries are stored separately in the Inflight Store
 * (Module 4.4) and linked via @p client_id.
 *
 * Session expiry semantics (MQTT 5.0 Section 3.1.2.11.2):
 * - @p session_expiry_interval == 0:           session expires on disconnect.
 * - @p session_expiry_interval == 0xFFFF'FFFF: session never expires.
 * - Any other value: session expires that many seconds after disconnect.
 */
struct SessionState {
    Utf8String              client_id;                   ///< Unique client identifier.
    std::vector<Subscription> subscriptions;             ///< Active subscriptions held by this session.
    uint32_t session_expiry_interval{0};                 ///< Session lifetime after disconnect, in seconds.

    bool operator==(const SessionState&) const noexcept = default;
};

} // namespace mqtt
