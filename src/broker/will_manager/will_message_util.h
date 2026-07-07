#pragma once

/**
 * @file will_message_util.h
 * @brief Utility functions for converting CONNECT will payloads to WillMessage.
 */

#include "data_model/message/message.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/property/property_id.h"

namespace mqtt {

/**
 * @brief Convert CONNECT will data to the internal WillMessage model.
 *
 * The Will Delay Interval property is extracted into
 * `WillMessage::delay_interval`; all remaining properties are preserved on the
 * embedded message.
 *
 * @param will_data CONNECT will payload.
 * @return Converted WillMessage model.
 */
[[nodiscard]] inline WillMessage
will_data_to_will_message(const WillData &will_data) {
  WillMessage will_message;
  will_message.message.topic = will_data.topic;
  will_message.message.payload = will_data.payload;
  will_message.message.qos = will_data.qos;
  will_message.message.retain = will_data.retain;

  for (const Property &property : will_data.properties) {
    if (property.id == PropertyId::WillDelayInterval) {
      will_message.delay_interval = std::get<FourByteInteger>(property.value);
      continue;
    }
    will_message.message.properties.push_back(property);
  }

  return will_message;
}

} // namespace mqtt
