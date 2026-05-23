#pragma once

/**
 * @file zwave_config.h
 * @brief ZWave domain configuration contract.
 */

#include "yaha/message/message.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yaha {

inline constexpr std::uint32_t kZwaveDefaultPollIntervalMs = 500U;
inline constexpr std::uint32_t kZwaveDefaultCommandReactionPollIntervalMs = 500U;
inline constexpr std::uint32_t kZwaveDefaultCommandReactionTimeoutMs = 30000U;
inline constexpr std::uint16_t kZwaveDefaultFileStorePort = 8210U;
inline constexpr std::uint32_t kZwaveDefaultFileStoreStartupRetryCount = 10U;
inline constexpr std::uint32_t kZwaveDefaultFileStoreStartupRetryIntervalSeconds = 60U;
inline constexpr std::string_view kZwaveDefaultFileStoreMonitorTopicPrefix = "$MONITOR/FileStore";

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
    std::uint8_t logLevel{2U};                ///< OpenZWave/service event logging level (0..4).
    bool logIncomingMessages{false};          ///< Logs incoming MQTT messages handled by ZWave service.
    bool logOutgoingMessages{false};          ///< Logs outgoing MQTT messages published by ZWave service.
    std::uint32_t pollIntervalMs{kZwaveDefaultPollIntervalMs}; ///< OpenZWave poll interval in milliseconds.
    std::uint32_t commandReactionPollIntervalMs{kZwaveDefaultCommandReactionPollIntervalMs}; ///< Poll interval in milliseconds for tracked command confirmation.
    std::uint32_t commandReactionTimeoutMs{kZwaveDefaultCommandReactionTimeoutMs}; ///< Timeout in milliseconds for tracked command confirmation.
    bool fileStoreEnabled{false};             ///< Enables startup device-settings sync with FileStore.
    std::string fileStoreHost{"127.0.0.1"}; ///< FileStore HTTP host.
    std::uint16_t fileStorePort{kZwaveDefaultFileStorePort}; ///< FileStore HTTP port.
    std::string settingsKeyPath{"/zwave/settings"}; ///< FileStore key path for ZWave settings JSON.
    std::string fileStoreMonitorTopicPrefix{std::string{kZwaveDefaultFileStoreMonitorTopicPrefix}}; ///< FileStore monitor topic prefix used for runtime reload trigger messages.
    std::uint32_t fileStoreStartupRetryCount{kZwaveDefaultFileStoreStartupRetryCount}; ///< Additional startup retries after first failed load.
    std::uint32_t fileStoreStartupRetryIntervalSeconds{kZwaveDefaultFileStoreStartupRetryIntervalSeconds}; ///< Wait interval between startup retries.
    ZwaveUsbConfig usb{};                     ///< USB/controller endpoint configuration.
    std::vector<ZwaveDeviceConfig> devices{}; ///< Required list of configured ZWave devices.
};

} // namespace yaha
