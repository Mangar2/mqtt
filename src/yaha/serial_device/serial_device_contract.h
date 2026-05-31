#pragma once

/**
 * @file serial_device_contract.h
 * @brief Domain contract and subscription derivation for YAHA SerialDevice client.
 */

#include "yaha/mqtt_component/mqtt_component.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace yaha {

inline constexpr std::uint32_t k_default_serialdevice_baudrate{38400U};
inline constexpr std::uint32_t k_default_serialdevice_keep_alive_delay_seconds{30U};

/**
 * @brief One switch topic mapping entry.
 */
struct SerialDeviceSwitchTopicMapping {
    std::string command{};
    std::uint16_t value{0U};
    std::string address{};
};

/**
 * @brief One value-map entry for serial value conversion.
 */
struct SerialDeviceValueMapDefinition {
    std::string description{};
    std::vector<std::string> usedBy{};
    std::unordered_map<std::string, std::uint16_t> map{};
};

/**
 * @brief One interface definition in serialdevice config.
 */
struct SerialDeviceInterfaceDefinition {
    std::unordered_map<std::string, std::string> commandMap{};
    std::unordered_map<std::string, std::string> sendMap{};
    std::unordered_map<std::string, std::string> receiverMap{};
    bool receiverMapProvided{false};
    std::unordered_map<std::string, SerialDeviceSwitchTopicMapping> topicMap{};
    std::unordered_map<std::string, SerialDeviceValueMapDefinition> valueMap{};
};

/**
 * @brief Domain configuration for serialdevice behavior.
 */
struct SerialDeviceConfig {
    std::string serialPortName{};
    std::uint32_t baudrate{k_default_serialdevice_baudrate};
    Qos subscribeQos{Qos::AtLeastOnce};
    std::string traceLevel{"internal"};
    bool logIncomingMessages{false};
    bool logOutgoingMessages{false};
    bool logReason{true};
    std::uint32_t keepAliveDelayInSeconds{k_default_serialdevice_keep_alive_delay_seconds};
    std::unordered_map<std::string, SerialDeviceInterfaceDefinition> interfaces{};
};

/**
 * @brief Derives MQTT subscriptions from serialdevice configuration.
 * @param config SerialDevice configuration.
 * @return Topic filter to QoS map.
 */
[[nodiscard]] SubscriptionMap deriveSerialDeviceSubscriptions(const SerialDeviceConfig& config);

} // namespace yaha
