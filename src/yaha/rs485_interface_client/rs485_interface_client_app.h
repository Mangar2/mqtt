#pragma once

/**
 * @file rs485_interface_client_app.h
 * @brief Runtime config types and INI mapping helpers for YAHA RS485 Interface standalone process.
 */

#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_component/mqtt_component.h"
#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"
#include "yaha/rs485_interface_client/rs485_serial_adapter.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace yaha {

inline constexpr std::uint32_t k_default_rs485_baudrate{57600U};
inline constexpr std::uint8_t k_default_rs485_my_address{1U};
inline constexpr std::uint8_t k_default_rs485_max_version{1U};
inline constexpr std::uint32_t k_default_rs485_tick_delay_ms{100U};
inline constexpr std::uint32_t k_default_rs485_time_of_day_delay_s{60U};
inline constexpr std::uint32_t k_default_rs485_blink_delay_s{3U};
inline constexpr std::uint32_t k_default_rs485_temporary_on_s{1200U};

/**
 * @brief One explicit topic mapping from MQTT to serial.
 */
struct Rs485TopicMapping {
    char command{'\0'};          ///< Serial command symbol.
    std::uint16_t value{0U};      ///< Serial value payload.
    std::uint8_t address{0U};     ///< Serial target address.
};

/**
 * @brief One interface definition used for string-value mapping.
 */
struct Rs485InterfaceDefinition {
    std::vector<char> usedBy{};                               ///< Commands using this interface mapping.
    std::unordered_map<std::string, std::uint16_t> map{};    ///< String-to-value map.
};

/**
 * @brief Domain configuration for RS485 Interface behavior.
 */
struct Rs485InterfaceConfig {
    std::string serialPortName{};                                            ///< Required serial port name.
    std::uint32_t baudrate{k_default_rs485_baudrate};                        ///< Serial baudrate.
    std::uint8_t myAddress{k_default_rs485_my_address};                      ///< Sender address.
    std::uint8_t maxVersion{k_default_rs485_max_version};                    ///< Maximum protocol version.
    std::uint32_t tickDelayMs{k_default_rs485_tick_delay_ms};                ///< Scheduler tick delay in milliseconds.
    std::uint32_t timeOfDayDelaySeconds{k_default_rs485_time_of_day_delay_s};///< Time-of-day broadcast delay in seconds.
    Qos subscribeQos{Qos::AtLeastOnce};                                      ///< MQTT subscription/publish QoS.
    std::string traceLevel{"messages"};                                    ///< Trace level string.
    std::uint32_t blinkDelaySeconds{k_default_rs485_blink_delay_s};          ///< Blink action delay in seconds.
    std::uint32_t temporaryOnSeconds{k_default_rs485_temporary_on_s};        ///< Default temporary-on duration in seconds.
    std::unordered_map<std::string, Rs485InterfaceDefinition> interfaces{};  ///< Interface definitions.
    std::unordered_map<char, std::string> settings{};                        ///< Setting command-to-topic-suffix mapping.
    std::unordered_map<char, std::string> status{};                          ///< Status command-to-topic-suffix mapping.
    std::unordered_map<std::string, std::uint8_t> addresses{};               ///< Topic-prefix-to-address mapping.
    std::unordered_map<std::string, Rs485TopicMapping> topics{};             ///< Explicit topic mappings.
};

/**
 * @brief Runtime config for RS485 standalone process.
 */
struct Rs485InterfaceRuntimeConfig {
    Rs485InterfaceConfig rs485Config{};  ///< RS485 domain config.
    YahaMqttClient::Config mqttConfig{}; ///< MQTT runtime config.
};

/**
 * @brief Runtime composition container for RS485 standalone process.
 */
struct Rs485InterfaceClientRuntimeObjects {
    Rs485InterfaceConfig rs485Config{};                      ///< RS485 config used for serial adapter open.
    std::unique_ptr<IMqttComponent> component{};          ///< Domain component instance.
    std::unique_ptr<Rs485SerialAdapter> serialAdapter{};  ///< Serial adapter instance.
    std::unique_ptr<YahaMqttClient> mqttClient{};         ///< MQTT client instance.
    std::unique_ptr<YahaMqttClientRuntime> runtime{};     ///< Generic runtime wrapper.
};

/**
 * @brief Loads RS485 domain config from INI document.
 * @param document Parsed INI document.
 * @param output Parsed config output.
 * @param errorMessage Human-readable parser error text.
 * @return True on success.
 */
[[nodiscard]] bool tryLoadRs485InterfaceConfigFromIni(
    const IniDocument& document,
    Rs485InterfaceConfig& output,
    std::string& errorMessage);

/**
 * @brief Loads full RS485 runtime config from INI document.
 * @param document Parsed INI document.
 * @param output Runtime config output.
 * @param errorMessage Human-readable parser error text.
 * @return True on success.
 */
[[nodiscard]] bool tryLoadRs485InterfaceClientRuntimeConfigFromIni(
    const IniDocument& document,
    Rs485InterfaceRuntimeConfig& output,
    std::string& errorMessage);

/**
 * @brief Builds runtime objects for RS485 standalone process composition.
 * @param runtimeConfig Parsed runtime configuration.
 * @param output Built runtime object bundle.
 * @param errorMessage Error text on failure.
 * @return True when all runtime objects were created and wired.
 */
[[nodiscard]] bool tryBuildRs485InterfaceClientRuntime(
    Rs485InterfaceRuntimeConfig runtimeConfig,
    Rs485InterfaceClientRuntimeObjects& output,
    std::string& errorMessage);

} // namespace yaha
