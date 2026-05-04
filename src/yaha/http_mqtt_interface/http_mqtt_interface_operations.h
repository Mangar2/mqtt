#pragma once

/**
 * @file http_mqtt_interface_operations.h
 * @brief Version 1.0 HTTP MQTT operation handlers and registry wiring.
 */

#include "yaha/http_mqtt_interface/http_mqtt_interface_dispatcher.h"

namespace yaha {

/**
 * @brief Builds handler registry containing HTTP MQTT version 1.0 operation handlers.
 * @return Handler registry for all request and response operations.
 */
[[nodiscard]] HttpMqttInterfaceHandlerRegistry makeHttpMqttInterfaceHandlerRegistryV1();

/**
 * @brief Builds HTTP MQTT interface facade pre-wired with version 1.0 handlers.
 * @return Interface facade with version 1.0 operation implementations.
 */
[[nodiscard]] HttpMqttInterfaces makeHttpMqttInterfacesV1();

} // namespace yaha
