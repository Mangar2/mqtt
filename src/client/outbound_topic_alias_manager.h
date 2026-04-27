#pragma once

/**
 * @file outbound_topic_alias_manager.h
 * @brief Outbound topic-alias assignment for client-side PUBLISH packets.
 */

#include <cstdint>
#include <string>
#include <unordered_map>

#include "data_model/packet/publish_packets.h"

namespace mqtt {

/**
 * @brief Tracks outbound topic alias mappings and rewrites PUBLISH packets.
 *
 * Rules:
 * - First publish of a topic: assign alias and keep topic string.
 * - Repeated publish of same topic: reuse alias and clear topic string.
 * - Active aliases never exceed configured maximum.
 */
class OutboundTopicAliasManager {
public:
  /**
   * @brief Construct alias manager with negotiated Topic Alias Maximum.
   * @param max_aliases Max alias value allowed by broker.
   */
  explicit OutboundTopicAliasManager(uint16_t max_aliases) noexcept;

  /**
   * @brief Apply aliasing rules to one outbound PUBLISH packet.
   * @param publish_packet Packet to modify in-place.
   * @return True when Topic Alias property is present after rewrite.
   */
  [[nodiscard]] bool apply(PublishPacket &publish_packet);

  /**
   * @brief Reset all alias mappings.
   */
  void reset() noexcept;

  /**
   * @brief Return configured alias maximum.
   */
  [[nodiscard]] uint16_t max_aliases() const noexcept;

private:
  [[nodiscard]] uint16_t allocate_alias();
  void bind_alias(uint16_t alias_value, const std::string &topic_name);
  void set_topic_alias_property(PublishPacket &publish_packet,
                                uint16_t alias_value) const;
  void advance_next_alias() noexcept;

  uint16_t max_aliases_{0U};
  uint16_t next_alias_{1U};
  std::unordered_map<std::string, uint16_t> topic_to_alias_;
  std::unordered_map<uint16_t, std::string> alias_to_topic_;
};

} // namespace mqtt
