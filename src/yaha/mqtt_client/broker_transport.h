#pragma once

/**
 * @file broker_transport.h
 * @brief Broker transport factory for YahaMqttClient.
 */

#include "yaha/mqtt_client/mqtt_client.h"

namespace yaha {

/**
 * @brief Creates the default broker-backed transport callback bundle.
 * @return Transport callbacks backed by TCP MQTT connection and packet codecs.
 */
[[nodiscard]] YahaMqttClient::Transport makeBrokerTransport();

} // namespace yaha
