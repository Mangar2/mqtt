#pragma once

/**
 * @file qos1_state_machine.h
 * @brief QoS 1 (AtLeastOnce) handshake state machine (Module 5.2).
 */

#include <cstdint>
#include <string>
#include <string_view>

#include "data_model/message/message.h"
#include "data_model/packet/publish_packets.h"
#include "qos/packet_id_manager.h"
#include "store/inflight_store.h"

namespace mqtt {

/**
 * @brief Per-session QoS 1 handshake handler (Module 5.2).
 *
 * Encapsulates all logic for QoS 1 (AtLeastOnce) exchanges — both
 * inbound (client → broker) and outbound (broker → client).
 *
 * All persistent state lives in the `InflightStore` and `PacketIdManager`
 * supplied at construction; this class itself is stateless beyond the
 * stored references.
 *
 * ### Inbound (5.2.1)
 * The broker receives a QoS 1 PUBLISH from a client and immediately returns
 * a PUBACK. No inflight entry is required — QoS 1 inbound is a single-step
 * acknowledgement.
 *
 * ### Outbound (5.2.2)
 * The broker initiates delivery to a subscribing client.  A Packet ID is
 * allocated and an `InflightEntry` is created.  When PUBACK arrives, the
 * entry is removed and the ID is released.
 *
 * ### Retransmission (5.2.3)
 * If the connection is lost and re-established before PUBACK arrives,
 * `retransmit()` rebuilds the PUBLISH with **DUP=true** and refreshes the
 * inflight entry timestamp.
 *
 * Thread safety: none — external synchronisation required.
 */
class Qos1StateMachine {
public:
  /**
   * @brief Construct a QoS 1 state machine for a single client session.
   * @param client_id Client identifier that owns this state machine.
   * @param id_mgr    Packet Identifier manager for the session.
   * @param store     Inflight message store for the session.
   */
  Qos1StateMachine(std::string_view client_id, PacketIdManager &id_mgr,
                   InflightStore &store);

  /**
   * @brief Inbound: process a received QoS 1 PUBLISH and return the PUBACK
   * (5.2.1).
   *
   * Does not create an inflight entry. No-op on duplicate PUBLISH (DUP=true) —
   * a fresh PUBACK is returned without side effects.
   *
   * @param pkt Validated QoS 1 PUBLISH packet.
   * @return PUBACK to transmit to the publishing client.
   * @throws QosException(InvalidPacket) if `pkt.qos != AtLeastOnce` or
   * `packet_id` is absent.
   */
  [[nodiscard]] static PubackPacket
  on_publish_received(const PublishPacket &pkt);

  /**
   * @brief Outbound: initiate delivery of a QoS 1 message (5.2.2).
   *
   * Allocates a new Packet ID, stores an `InflightEntry`, and constructs the
   * wire packet.
   *
   * @param msg Message to deliver; `msg.qos` must equal `QoS::AtLeastOnce`.
   * @return PUBLISH packet to send to the subscribing client (DUP=false).
   * @throws QosException(InvalidPacket) if `msg.qos != AtLeastOnce`.
   * @throws QosException(PacketIdExhausted) if no free Packet ID is available.
   */
  [[nodiscard]] PublishPacket initiate_publish(const Message &msg);

  /**
   * @brief Outbound: handle a received PUBACK and complete the exchange
   * (5.2.2).
   *
   * Removes the matching `InflightEntry` and releases the Packet ID.
   *
   * @param pkt PUBACK received from the subscribing client.
   * @throws QosException(UnexpectedPacketId) if no matching inflight entry
   * exists.
   */
  void on_puback_received(const PubackPacket &pkt);

  /**
   * @brief Outbound: retransmit a pending PUBLISH with DUP=true (5.2.3).
   *
   * Reconstructs the PUBLISH from the stored `InflightEntry` and refreshes its
   * timestamp in the inflight store.
   *
   * @param packet_id Packet Identifier of the pending outbound exchange.
   * @return PUBLISH with `dup=true` to resend to the subscribing client.
   * @throws QosException(UnexpectedPacketId) if no matching entry exists.
   */
  [[nodiscard]] PublishPacket retransmit(uint16_t packet_id);

private:
  std::string client_id_; ///< Owning client session identifier.
  PacketIdManager
      &id_mgr_; ///< Packet ID allocator (shared with QoS 2 for same session).
  InflightStore &store_; ///< Inflight message store.
};

} // namespace mqtt
