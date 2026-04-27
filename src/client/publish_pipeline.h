#pragma once

/**
 * @file publish_pipeline.h
 * @brief Client-side outbound publish pipeline for QoS progression.
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

#include "codec/write_buffer.h"
#include "client/client_error.h"
#include "data_model/message/message.h"
#include "data_model/packet/publish_packets.h"
#include "data_model/session/inflight_entry.h"
#include "qos/packet_id_manager.h"

namespace mqtt {

/**
 * @brief Client-side outbound publish pipeline.
 *
 * Creates PUBLISH packets from caller messages, tracks pending QoS 1/2
 * exchanges, and advances/finalizes flows based on PUBACK/PUBREC/PUBCOMP.
 */
class ClientPublishPipeline {
public:
  /**
   * @brief Result of begin_publish().
   */
  struct PublishStartResult {
    PublishPacket publish_packet;
    std::optional<uint16_t> packet_id;
    bool completed{false};
  };

  /**
   * @brief Result of processing a publish acknowledgement packet.
   */
  struct PublishAckResult {
    bool completed{false};
    bool send_pubrel{false};
    std::optional<PubrelPacket> pubrel_packet;
    ReasonCode reason_code{ReasonCode::Success};
  };

  /**
   * @brief Build outbound publish packet and register QoS tracking if needed.
   * @param message Outbound message model.
   * @return Start result containing PUBLISH packet and tracking status.
   * @throws ClientException when topic is invalid or packet-id allocation fails.
   */
  [[nodiscard]] PublishStartResult begin_publish(const Message &message);

  /**
   * @brief Process PUBACK for QoS 1 publish completion.
   * @param puback_packet Received PUBACK packet.
   * @return Publish acknowledgement result.
   * @throws ClientException on unknown packet-id or wrong QoS stage.
   */
  [[nodiscard]] PublishAckResult on_puback(const PubackPacket &puback_packet);

  /**
   * @brief Process PUBREC for QoS 2 publish progression.
   * @param pubrec_packet Received PUBREC packet.
   * @return Publish acknowledgement result.
   * @throws ClientException on unknown packet-id or wrong QoS stage.
   */
  [[nodiscard]] PublishAckResult on_pubrec(const PubrecPacket &pubrec_packet);

  /**
   * @brief Process PUBCOMP for QoS 2 publish completion.
   * @param pubcomp_packet Received PUBCOMP packet.
   * @return Publish acknowledgement result.
   * @throws ClientException on unknown packet-id or wrong QoS stage.
   */
  [[nodiscard]] PublishAckResult on_pubcomp(const PubcompPacket &pubcomp_packet);

  /**
   * @brief Return whether packet-id currently has pending QoS state.
   * @param packet_id Packet identifier to check.
   */
  [[nodiscard]] bool has_pending(uint16_t packet_id) const noexcept;

  /**
   * @brief Return number of pending QoS exchanges.
   */
  [[nodiscard]] std::size_t pending_count() const noexcept;

  /**
   * @brief Clear all pending state and release packet-id usage.
   */
  void clear() noexcept;

  /**
   * @brief Encode one outbound PUBLISH frame.
   * @param publish_packet Packet to encode.
   * @return Wire-ready frame.
   */
  [[nodiscard]] static WriteBuffer
  encode_publish_frame(const PublishPacket &publish_packet);

  /**
   * @brief Encode one outbound PUBREL frame.
   * @param pubrel_packet Packet to encode.
   * @return Wire-ready frame.
   */
  [[nodiscard]] static WriteBuffer encode_pubrel_frame(const PubrelPacket &pubrel_packet);

private:
  struct PendingPublish {
    InflightEntry inflight_entry;
  };

  [[nodiscard]] static InflightState initial_state_for_qos(QoS qos);
  static void validate_topic_or_throw(std::string_view topic_name);
  [[nodiscard]] uint16_t allocate_packet_id_or_throw();
  void release_packet_id(uint16_t packet_id) noexcept;
  [[nodiscard]] PendingPublish &find_pending_or_throw(uint16_t packet_id);

  PacketIdManager packet_id_manager_;
  std::unordered_map<uint16_t, PendingPublish> pending_publishes_;
};

} // namespace mqtt
