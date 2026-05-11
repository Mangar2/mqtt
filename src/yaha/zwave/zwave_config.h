#pragma once

/**
 * @file zwave_config.h
 * @brief ZWave domain configuration contract.
 */

#include "yaha/message/message.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yaha {

/**
 * @brief USB controller endpoint configuration for ZWave runtime.
 */
struct ZwaveUsbConfig {
    std::string device{}; ///< USB device path or identifier.
    std::string topic{};  ///< MQTT topic used for controller/root node reporting.
};

/**
 * @brief One configured ZWave device mapping entry.
 */
struct ZwaveDeviceConfig {
    std::string topic{};                       ///< MQTT topic prefix for this device mapping.
    std::uint16_t nodeId{0U};                 ///< ZWave node id.
    std::optional<std::uint16_t> classId{};   ///< Optional ZWave command class id.
    std::optional<std::uint8_t> instance{};   ///< Optional instance id.
    std::optional<std::uint8_t> index{};      ///< Optional value index.
    std::optional<std::string> type{};        ///< Optional value type hint.
    std::optional<std::string> label{};       ///< Optional command-class label.
};

/**
 * @brief Runtime configuration for ZWave component behavior.
 */
struct ZwaveConfig {
    Qos subscribeQos{Qos::AtLeastOnce};       ///< MQTT subscribe QoS for inbound commands.
    Qos qos{Qos::AtLeastOnce};                ///< MQTT publish QoS for outbound messages.
    bool retain{false};                       ///< MQTT retain flag for outbound messages.
    ZwaveUsbConfig usb{};                     ///< USB/controller endpoint configuration.
    std::vector<ZwaveDeviceConfig> devices{}; ///< Required list of configured ZWave devices.
};

} // namespace yaha
