#pragma once

/**
 * @file qos2_state_machine.h
 * @brief QoS 2 (ExactlyOnce) handshake state machine (Module 5.3).
 */

#include <cstdint>
#include <unordered_set>
#include <string>
#include <string_view>
#include <variant>

#include "data_model/message/message.h"
#include "data_model/packet/publish_packets.h"
#include "qos/packet_id_manager.h"
#include "store/inflight_store.h"

namespace mqtt {

/**
 * @brief Result of processing an inbound QoS 2 PUBLISH (5.3.1).
 *
 * When `is_duplicate` is `true` the message has already been received and
 * stored; the caller must **not** re-deliver it to subscribers.  The `pubrec`
 * field is always populated and must be sent to unblock the client.
 */
struct Qos2InboundPublishResult {
  PubrecPacket pubrec;      ///< PUBREC to transmit to the publishing client.
  bool is_duplicate{false}; ///< True = this Packet ID was already registered.
};

/**
 * @brief Per-session QoS 2 handshake handler (Module 5.3).
 *
 * Encapsulates all logic for QoS 2 (ExactlyOnce) exchanges — both inbound
 * (client → broker) and outbound (broker → client).  All persistent state
 * lives in the `InflightStore` and `PacketIdManager` supplied at construction.
 *
 * ### Inbound four-step handshake (5.3.1)
 * ```
 * Client       Broker
 *   PUBLISH▶         on_publish_received  → PUBREC
 *   ◀PUBREC          (inflight entry: WaitingForPubrel)
 *   PUBREL▶          on_pubrel_received   → PUBCOMP
 *   ◀PUBCOMP         (entry removed)
 * ```
 *
 * ### Outbound four-step handshake (5.3.2)
 * ```
 * Broker       Client
 *   PUBLISH▶         initiate_publish     (entry: WaitingForPubrec)
 *   ◀PUBREC          on_pubrec_received   → PUBREL (entry:
 * WaitingForPubcomp) PUBREL▶ ◀PUBCOMP         on_pubcomp_received
 * (entry removed)
 * ```
 *
 * ### Duplicate detection (5.3.3)
 * `on_publish_received` calls `PacketIdManager::try_register_inbound`.  If the
 * ID is already registered the PUBLISH is a retransmission; PUBREC is still
 * returned but `Qos2InboundPublishResult::is_duplicate` is set to `true`.
 *
 * ### Retransmission (5.3.4)
 * `retransmit()` inspects the current handshake phase and returns either a
 * duplicate PUBLISH (DUP=true, phase WaitingForPubrec) or a PUBREL (phase
 * WaitingForPubcomp) as a `std::variant<PublishPacket, PubrelPacket>`.
 *
 * Thread safety: none — external synchronisation required.
 */
class Qos2StateMachine {
public:
  /**
   * @brief Construct a QoS 2 state machine for a single client session.
   * @param client_id Client identifier that owns this state machine.
   * @param id_mgr    Packet Identifier manager for the session.
   * @param store     Inflight message store for the session.
   */
  Qos2StateMachine(std::string_view client_id, PacketIdManager &id_mgr,
                   InflightStore &store);

  /**
   * @brief Inbound: process a received QoS 2 PUBLISH (5.3.1, 5.3.3).
   *
   * Registers the inbound Packet ID via
   * `PacketIdManager::try_register_inbound`. If new: creates an
   * `InflightEntry(WaitingForPubrel)`. If duplicate: does not modify the store.
   *
   * @param pkt Validated QoS 2 PUBLISH packet.
   * @return PUBREC to send and a duplicate flag.
   * @throws QosException(InvalidPacket) if `pkt.qos != ExactlyOnce` or
   * `packet_id` is absent.
   */
  [[nodiscard]] Qos2InboundPublishResult on_publish_received(const PublishPacket &pkt);

  /**
   * @brief Inbound: handle a received PUBREL and return PUBCOMP (5.3.1).
   *
   * Removes the inbound `InflightEntry` and releases the inbound Packet ID.
   *
   * @param pkt PUBREL received from the publishing client.
   * @return PUBCOMP to transmit to the client.
   * @throws QosException(UnexpectedPacketId) if no matching inbound entry
   * exists.
   */
  [[nodiscard]] PubcompPacket on_pubrel_received(const PubrelPacket &pkt);

  /**
   * @brief Abort an inbound QoS 2 exchange without sending PUBCOMP.
   *
   * Used when the broker responds to the initial PUBLISH with an error PUBREC
   * (for example Not Authorized) and must not continue the handshake.
   *
   * @param packet_id Inbound packet identifier to clear.
   */
  void abort_inbound(uint16_t packet_id) noexcept;

  /**
   * @brief Outbound: initiate delivery of a QoS 2 message (5.3.2).
   *
   * Allocates a Packet ID, stores an `InflightEntry(WaitingForPubrec)`, and
   * returns the initial PUBLISH.
   *
   * @param msg Message to deliver; `msg.qos` must equal `QoS::ExactlyOnce`.
   * @return PUBLISH to send to the subscribing client (DUP=false).
   * @throws QosException(InvalidPacket) if `msg.qos != ExactlyOnce`.
   * @throws QosException(PacketIdExhausted) if no free Packet ID is available.
   */
  [[nodiscard]] PublishPacket initiate_publish(const Message &msg);

  /**
   * @brief Outbound: handle a received PUBREC and return PUBREL (5.3.2).
   *
   * Advances the inflight entry to `WaitingForPubcomp`. Idempotent on a
   * duplicate PUBREC — always returns PUBREL.
   *
   * @param pkt PUBREC received from the subscribing client.
   * @return PUBREL to transmit to the client.
   * @throws QosException(UnexpectedPacketId) if no matching outbound entry
   * exists.
   */
  [[nodiscard]] PubrelPacket on_pubrec_received(const PubrecPacket &pkt);

  /**
   * @brief Outbound: handle a received PUBCOMP and complete the exchange
   * (5.3.2).
   *
   * Removes the outbound `InflightEntry` and releases the Packet ID.
   *
   * @param pkt PUBCOMP received from the subscribing client.
   * @throws QosException(UnexpectedPacketId) if no matching outbound entry
   * exists.
   */
  void on_pubcomp_received(const PubcompPacket &pkt);

  /**
   * @brief Outbound: retransmit based on the current handshake phase (5.3.4).
   *
   * Inspects the inflight entry state and returns:
   * - `PublishPacket` (DUP=true) if the entry is in `WaitingForPubrec`.
   * - `PubrelPacket` if the entry is in `WaitingForPubcomp`.
   *
   * Refreshes the inflight entry timestamp in both cases.
   *
   * @param packet_id Packet Identifier of the pending outbound exchange.
   * @return Variant holding the packet to retransmit.
   * @throws QosException(UnexpectedPacketId) if no matching outbound entry
   * exists.
   */
  [[nodiscard]] std::variant<PublishPacket, PubrelPacket> retransmit(uint16_t packet_id);

private:
  std::string client_id_; ///< Owning client session identifier.
  PacketIdManager
      &id_mgr_; ///< Packet ID allocator (shared with QoS 1 for same session).
  InflightStore &store_; ///< Inflight message store.
  std::unordered_set<uint16_t>
      recently_completed_inbound_; ///< Packet IDs completed via PUBCOMP.
};

} // namespace mqtt
