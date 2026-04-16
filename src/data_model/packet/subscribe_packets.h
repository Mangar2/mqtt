#pragma once

/**
 * @file subscribe_packets.h
 * @brief MQTT 5.0 SUBSCRIBE, SUBACK, UNSUBSCRIBE, and UNSUBACK packet structs
 * (Sections 3.8–3.11).
 */

#include "data_model/property/property.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/types/qos.h"
#include "data_model/types/utf8_string.h"
#include <cstdint>
#include <vector>

namespace mqtt {

/**
 * @brief Per-filter subscription options (Section 3.8.3.1).
 */
struct SubscribeOptions {
  QoS max_qos{QoS::AtMostOnce}; ///< Maximum QoS level the broker may use.
  bool no_local{false}; ///< Do not deliver the publisher's own messages.
  bool retain_as_published{false}; ///< Forward the RETAIN flag as received.
  uint8_t retain_handling{
      0}; ///< 0 = send on subscribe, 1 = only if new, 2 = never.

  bool operator==(const SubscribeOptions &) const noexcept = default;
};

/**
 * @brief One topic filter + options entry in a SUBSCRIBE packet.
 */
struct SubscribeFilter {
  Utf8String topic_filter;  ///< Topic filter string.
  SubscribeOptions options; ///< Subscription options for this filter.

  bool operator==(const SubscribeFilter &) const noexcept = default;
};

/**
 * @brief SUBSCRIBE packet (Section 3.8).
 */
struct SubscribePacket {
  uint16_t packet_id{0};
  std::vector<Property> properties;
  std::vector<SubscribeFilter>
      filters; ///< At least one filter required by the spec.

  bool operator==(const SubscribePacket &) const noexcept = default;
};

/**
 * @brief SUBACK packet (Section 3.9).
 *
 * @p reason_codes contains one entry per filter in the corresponding SUBSCRIBE.
 */
struct SubackPacket {
  uint16_t packet_id{0};
  std::vector<Property> properties;
  std::vector<ReasonCode>
      reason_codes; ///< One result code per SUBSCRIBE filter.

  bool operator==(const SubackPacket &) const noexcept = default;
};

/**
 * @brief UNSUBSCRIBE packet (Section 3.10).
 */
struct UnsubscribePacket {
  uint16_t packet_id{0};
  std::vector<Property> properties;
  std::vector<Utf8String>
      topic_filters; ///< At least one filter required by the spec.

  bool operator==(const UnsubscribePacket &) const noexcept = default;
};

/**
 * @brief UNSUBACK packet (Section 3.11).
 *
 * @p reason_codes contains one entry per filter in the corresponding
 * UNSUBSCRIBE.
 */
struct UnsubackPacket {
  uint16_t packet_id{0};
  std::vector<Property> properties;
  std::vector<ReasonCode>
      reason_codes; ///< One result code per UNSUBSCRIBE filter.

  bool operator==(const UnsubackPacket &) const noexcept = default;
};

} // namespace mqtt
