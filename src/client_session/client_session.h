#pragma once

/**
 * @file client_session.h
 * @brief ClientSession — per-connection session context that owns packet-level
 * state machines and outbound drain logic (Module 21).
 */

#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>


#include "auth/authenticator.h"
#include "auth/enhanced_auth_handler.h"
#include "codec/write_buffer.h"
#include "connection/connection_state.h"
#include "connection/keep_alive_timer.h"
#include "connection/receive_maximum.h"
#include "connection/topic_alias_table.h"
#include "data_model/message/message.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/packet/control_packets.h"
#include "data_model/packet/publish_packets.h"
#include "outbound_queue/outbound_queue.h"
#include "qos/packet_id_manager.h"
#include "qos/qos1_state_machine.h"
#include "qos/qos2_state_machine.h"
#include "store/inflight_store.h"

namespace mqtt {

/**
 * @brief Result of handling one inbound PUBLISH packet.
 */
struct InboundPublishResult {
  std::optional<Message> routable_message; ///< Message for broker routing;
                                           ///< empty for QoS2 duplicates.
  std::vector<WriteBuffer>
      response_frames; ///< Encoded ACK frames to send immediately.
};

/**
 * @brief Per-client session context and packet handlers (Module 21).
 *
 * Owns all per-connection state needed by the lean client handler:
 * QoS state machines, Receive Maximum controller, topic aliases, keep-alive,
 * connection lifecycle state, enhanced AUTH state, and outbound queue drain
 * orchestration.
 *
 * Thread safety: none. Must be used by one owning client thread.
 */
class ClientSession {
public:
  /**
   * @brief Construct a client session context.
   *
   * The internal `ConnectionStateMachine` is moved to `Connected` during
   * construction because this object is intended for post-CONNECT processing.
   *
   * @param client_id Owning client identifier.
   * @param username Authenticated username (may be empty).
   * @param authenticator Auth backend used by EnhancedAuthHandler.
   * @param outbound_queue Shared per-client outbound queue.
   * @param inflight_store Shared inflight store for QoS state machines.
   * @param keep_alive_seconds Keep Alive interval from CONNECT.
   * @param receive_maximum Receive Maximum from peer/broker config.
   * @param topic_alias_maximum Topic Alias Maximum for this connection.
   * @throws std::invalid_argument if @p outbound_queue is null.
   */
  ClientSession(std::string client_id, std::string username,
                std::shared_ptr<IAuthenticator> authenticator,
                std::shared_ptr<OutboundQueue> outbound_queue,
                InflightStore &inflight_store, uint16_t keep_alive_seconds,
                uint16_t receive_maximum, uint16_t topic_alias_maximum,
          std::chrono::steady_clock::duration retransmit_timeout =
            std::chrono::seconds{20},
          uint32_t maximum_packet_size = 0U,
          std::string negotiated_auth_method = "");

  /**
   * @brief Start enhanced authentication state from CONNECT.
   * @param connect_packet CONNECT packet used to initialize auth state.
   * @return Result from `EnhancedAuthHandler::initiate()`.
   */
  [[nodiscard]] AuthResult initiate_auth(const ConnectPacket &connect_packet);

  /**
   * @brief Handle inbound PUBLISH and build immediate response frames.
   * @param publish_packet Incoming PUBLISH packet.
   * @return Message for routing plus encoded ACK frames.
   */
  [[nodiscard]] InboundPublishResult
  on_publish(const PublishPacket &publish_packet);

  /**
   * @brief Handle inbound PUBACK and free one outbound inflight slot.
   * @param puback_packet Incoming PUBACK packet.
   */
  void on_puback(const PubackPacket &puback_packet);

  /**
   * @brief Handle inbound PUBREC and return encoded PUBREL.
   * @param pubrec_packet Incoming PUBREC packet.
   * @return Encoded PUBREL frame.
   */
  [[nodiscard]] WriteBuffer on_pubrec(const PubrecPacket &pubrec_packet);

  /**
   * @brief Handle inbound PUBREL and return encoded PUBCOMP.
   * @param pubrel_packet Incoming PUBREL packet.
   * @return Encoded PUBCOMP frame.
   */
  [[nodiscard]] WriteBuffer on_pubrel(const PubrelPacket &pubrel_packet);

  /**
   * @brief Abort a pending inbound QoS 2 exchange for one Packet ID.
   * @param packet_id Inbound QoS 2 Packet Identifier.
   */
  void abort_inbound_qos2(uint16_t packet_id) noexcept;

  /**
   * @brief Handle inbound PUBCOMP and free one outbound inflight slot.
   * @param pubcomp_packet Incoming PUBCOMP packet.
   */
  void on_pubcomp(const PubcompPacket &pubcomp_packet);

  /**
   * @brief Handle inbound AUTH packet for enhanced auth / re-auth.
   * @param auth_packet Incoming AUTH packet.
   * @return Result from enhanced auth handler.
   */
  [[nodiscard]] AuthResult on_auth(const AuthPacket &auth_packet);

  /**
   * @brief Drain outbound queue and encode packets to send on wire.
   *
   * QoS 0 messages are encoded directly. QoS 1/2 messages are passed through
   * their state machines and gated by `ReceiveMaximum`.
   *
   * @return Encoded MQTT packet frames ready to enqueue to transport write
   * queue.
   */
  [[nodiscard]] std::vector<WriteBuffer> drain_outbound();

  /** @brief Return client identifier. */
  [[nodiscard]] std::string_view client_id() const noexcept;

  /** @brief Return authenticated username. */
  [[nodiscard]] std::string_view username() const noexcept;

  /** @brief Return negotiated enhanced Authentication Method token. */
  [[nodiscard]] std::string_view negotiated_auth_method() const noexcept;

  /** @brief Return per-session receive-maximum controller. */
  [[nodiscard]] ReceiveMaximum &receive_maximum() noexcept;

  /** @brief Return per-session topic alias table. */
  [[nodiscard]] TopicAliasTable &topic_alias_table() noexcept;

  /** @brief Return per-session keep-alive timer. */
  [[nodiscard]] KeepAliveTimer &keep_alive_timer() noexcept;

  /** @brief Return per-session connection state machine. */
  [[nodiscard]] ConnectionStateMachine &connection_state_machine() noexcept;

  /**
   * @brief Mark this connection as a resumed session.
   *
   * The next drain cycle replays outbound inflight QoS entries immediately,
   * instead of waiting for retransmission timeout.
   */
  void mark_session_resumed() noexcept;

private:
  /**
   * @brief Convert an inbound PUBLISH into the broker message model.
   * @param publish_packet Source packet.
   * @return Converted message.
   */
  [[nodiscard]] static Message
  message_from_publish(const PublishPacket &publish_packet);

  /**
   * @brief Convert a message to a QoS 0 PUBLISH packet.
   * @param message Message to encode.
   * @return Wire-level PUBLISH packet.
   */
  [[nodiscard]] static PublishPacket
  qos0_publish_from_message(const Message &message);

  /** @brief Encode one PUBACK packet. */
  [[nodiscard]] static WriteBuffer
  encode_puback_packet(const PubackPacket &pkt);

  /** @brief Encode one PUBREC packet. */
  [[nodiscard]] static WriteBuffer
  encode_pubrec_packet(const PubrecPacket &pkt);

  /** @brief Encode one PUBREL packet. */
  [[nodiscard]] static WriteBuffer
  encode_pubrel_packet(const PubrelPacket &pkt);

  /** @brief Encode one PUBCOMP packet. */
  [[nodiscard]] static WriteBuffer
  encode_pubcomp_packet(const PubcompPacket &pkt);

  /** @brief Encode one PUBLISH packet. */
  [[nodiscard]] static WriteBuffer
  encode_publish_packet(const PublishPacket &pkt);

  /// Return true when a message can be encoded into a PUBLISH frame that
  /// respects the negotiated Maximum Packet Size limit.
  [[nodiscard]] bool
  is_outbound_publish_within_maximum_packet_size(const Message &message) const;

  /// Build a representative outbound PUBLISH packet for frame-size checks.
  [[nodiscard]] static PublishPacket
  publish_from_message_for_size_check(const Message &message);

  /**
   * @brief Pop one outbound message from deferred or shared queue.
   * @return Next outbound message, if any.
   */
  [[nodiscard]] std::optional<Message> pop_next_message();

  /// Emit retransmission frames for overdue outbound inflight entries.
  void append_retransmission_frames(std::vector<WriteBuffer> &frames);

  std::string client_id_;                         ///< Owning client identifier.
  std::string username_;                          ///< Authenticated username.
  std::shared_ptr<OutboundQueue> outbound_queue_; ///< Shared outbound queue.
  PacketIdManager packet_id_manager_;             ///< Per-session Packet IDs.
  Qos1StateMachine qos1_state_machine_;           ///< QoS 1 state machine.
  Qos2StateMachine qos2_state_machine_;           ///< QoS 2 state machine.
  ReceiveMaximum receive_maximum_;                ///< Outbound inflight limit.
  TopicAliasTable topic_alias_table_;             ///< Topic alias mappings.
  KeepAliveTimer keep_alive_timer_;               ///< Keep-alive deadline.
  ConnectionStateMachine connection_state_machine_; ///< Lifecycle state.
  EnhancedAuthHandler enhanced_auth_handler_;       ///< AUTH state machine.
  InflightStore &inflight_store_;                   ///< Session inflight store.
  std::chrono::steady_clock::duration
      retransmit_timeout_; ///< QoS retransmit timeout.
  uint32_t maximum_packet_size_; ///< 0 means no maximum packet size limit.
  bool replay_pending_inflight_{false}; ///< Immediate inflight replay on resume.
  std::deque<Message>
      deferred_messages_; ///< QoS 1/2 messages parked while receive-max paused.
};

} // namespace mqtt
