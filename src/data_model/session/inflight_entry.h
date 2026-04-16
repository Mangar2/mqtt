#pragma once

/**
 * @file inflight_entry.h
 * @brief In-flight message entry for QoS 1 and QoS 2 exchanges (Module 1.7.2).
 */

#include <chrono>
#include <cstdint>

#include "data_model/message/message.h"
#include "data_model/session/inflight_direction.h"
#include "data_model/session/inflight_state.h"
#include "data_model/types/qos.h"

namespace mqtt {

/**
 * @brief A pending QoS 1 or QoS 2 handshake record (Module 1.7.2).
 *
 * Stored in the Inflight Store (Module 4.4) for every unacknowledged exchange.
 * The @p timestamp records when the message was last transmitted; the QoS Engine
 * (Module 5) uses it to schedule retransmissions with the DUP flag set.
 */
struct InflightEntry {
    uint16_t          packet_id{0};                                    ///< Packet Identifier; unique within a session direction.
    Message           message;                                         ///< Copy of the message under exchange.
    QoS               qos{QoS::AtLeastOnce};                           ///< QoS level of this exchange (1 or 2).
    InflightState     state{InflightState::WaitingForPuback};          ///< Current handshake phase.
    InflightDirection direction{InflightDirection::Outbound};          ///< Direction of the exchange.
    std::chrono::steady_clock::time_point timestamp{};                 ///< Time of last transmission; used for retry scheduling.

    bool operator==(const InflightEntry&) const noexcept = default;
};

} // namespace mqtt
