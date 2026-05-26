#pragma once

/**
 * @file serial_device_client_app.h
 * @brief Runtime composition helpers for YAHA SerialDevice standalone process.
 */

#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"
#include "yaha/serial_device/serial_device_component.h"
#include "yaha/serial_device_client/serial_device_client_config.h"

#include <memory>

namespace yaha {

/**
 * @brief Runtime object bundle for SerialDevice standalone process.
 */
struct SerialDeviceClientRuntimeObjects {
    SerialDeviceClientRuntimeConfig runtimeConfig{};               ///< Effective runtime configuration.
    std::unique_ptr<SerialDeviceComponent> component{};            ///< SerialDevice domain component instance.
    std::shared_ptr<ISerialDeviceTransport> serialTransport{};     ///< Serial transport instance used by component.
    std::unique_ptr<YahaMqttClient> mqttClient{};                  ///< MQTT client instance.
    std::unique_ptr<YahaMqttClientRuntime> runtime{};              ///< Generic runtime wrapper.
};

/**
 * @brief Builds runtime objects for SerialDevice standalone process composition.
 * @param runtimeConfig Parsed runtime configuration.
 * @return Built runtime object bundle.
 */
[[nodiscard]] SerialDeviceClientRuntimeObjects buildSerialDeviceClientRuntime(
    const SerialDeviceClientRuntimeConfig& runtimeConfig);

} // namespace yaha
