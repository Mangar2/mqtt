#pragma once

/**
 * @file inflight_state.h
 * @brief QoS handshake phase for in-flight messages (Module 1.7.2).
 */

#include <cstdint>

namespace mqtt {

/**
 * @brief Current phase of an in-flight QoS 1 or QoS 2 handshake (Module 1.7.2).
 *
 * State transitions per direction:
 * - QoS 1 outbound:  WaitingForPuback (terminal on PUBACK)
 * - QoS 2 outbound:  WaitingForPubrec → WaitingForPubcomp (terminal on PUBCOMP)
 * - QoS 2 inbound:   WaitingForPubrel (terminal on PUBREL → send PUBCOMP)
 *
 * State-machine logic lives in the QoS Engine (Module 5).
 */
enum class InflightState : uint8_t {
    WaitingForPuback  = 0,  ///< QoS 1 outbound: PUBLISH sent; awaiting PUBACK.
    WaitingForPubrec  = 1,  ///< QoS 2 outbound: PUBLISH sent; awaiting PUBREC.
    WaitingForPubrel  = 2,  ///< QoS 2 inbound:  PUBREC sent; awaiting PUBREL from client.
    WaitingForPubcomp = 3,  ///< QoS 2 outbound: PUBREL sent; awaiting PUBCOMP.
};

} // namespace mqtt
