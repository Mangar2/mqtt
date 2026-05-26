#pragma once

/**
 * @file serial_device_mqtt_to_serial_mapper.h
 * @brief MQTT topic/value to SerialDevice internal message mapper.
 */

#include "yaha/serial_device/serial_device_contract.h"
#include "yaha/serial_device/serial_device_message.h"

#include <string>

namespace yaha {

/**
 * @brief Maps one MQTT topic/value pair to one serial-device message.
 * @param configValue SerialDevice configuration.
 * @param topicValue Incoming MQTT topic.
 * @param valueValue Incoming MQTT value as text.
 * @return Mapped serial-device message.
 * @throws std::runtime_error For unknown topic mapping or invalid value conversion.
 */
[[nodiscard]] SerialDeviceMessage mapMqttToSerialMessage(
    const SerialDeviceConfig& configValue,
    const std::string& topicValue,
    const std::string& valueValue);

} // namespace yaha
