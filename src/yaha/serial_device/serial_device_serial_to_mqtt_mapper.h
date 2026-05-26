#pragma once

/**
 * @file serial_device_serial_to_mqtt_mapper.h
 * @brief SerialDevice internal message to MQTT publish message mapper.
 */

#include "yaha/message/message.h"
#include "yaha/serial_device/serial_device_contract.h"
#include "yaha/serial_device/serial_device_message.h"

#include <vector>

namespace yaha {

/**
 * @brief Maps one serial-device message to zero or more MQTT publish messages.
 * @param configValue SerialDevice configuration.
 * @param serialMessage Serial-device message.
 * @return MQTT publish messages in deterministic order.
 * @throws std::runtime_error For unknown interface/address/command mapping errors.
 */
[[nodiscard]] std::vector<Message> mapSerialMessageToMqttMessages(
    const SerialDeviceConfig& configValue,
    const SerialDeviceMessage& serialMessage);

} // namespace yaha
