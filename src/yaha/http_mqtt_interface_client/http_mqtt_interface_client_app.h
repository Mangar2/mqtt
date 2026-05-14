#pragma once

/**
 * @file http_mqtt_interface_client_app.h
 * @brief Runtime configuration and server entry point for the standalone HTTP MQTT interface client.
 */

#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_component/mqtt_component.h"
#include "yaha/mqtt_client/mqtt_client.h"

#include <cstdint>
#include <memory>
#include <string>

namespace yaha {

constexpr std::uint16_t k_defaultHttpMqttInterfaceListenerPort{8092U};

/**
 * @brief Runtime settings for the standalone HTTP MQTT interface client.
 */
struct HttpMqttInterfaceClientConfig {
    std::string listenerHost{"127.0.0.1"};  ///< HTTP bind host.
    std::uint16_t listenerPort{k_defaultHttpMqttInterfaceListenerPort};  ///< HTTP bind port.
    bool enablePublishPhpAlias{true};         ///< Enables POST /publish.php compatibility alias.
    bool useLegacyPhpResponse{false};         ///< Enables legacy 200 JSON-string response mode.
    YahaMqttClient::Config mqttConfig{};      ///< Broker publish transport runtime settings.
};

/**
 * @brief IMqttComponent implementation for standalone HTTP MQTT interface domain runtime.
 */
class HttpMqttInterfaceClientComponent final : public IMqttComponent {
public:
    /**
     * @brief Constructs component with runtime configuration.
     * @param configInput Runtime configuration.
     */
    explicit HttpMqttInterfaceClientComponent(HttpMqttInterfaceClientConfig configInput);

    /**
     * @brief Destructor stops active HTTP server thread.
     */
    ~HttpMqttInterfaceClientComponent() override;

    HttpMqttInterfaceClientComponent(const HttpMqttInterfaceClientComponent&) = delete;
    HttpMqttInterfaceClientComponent& operator=(const HttpMqttInterfaceClientComponent&) = delete;

    /**
     * @brief Returns subscriptions for this component.
     * @return Empty map because this component is publish-forward focused.
     */
    [[nodiscard]] SubscriptionMap getSubscriptions() const override;

    /**
     * @brief Handles inbound MQTT messages.
     * @param message Incoming message.
     */
    void handleMessage(const Message& message) override;

    /**
     * @brief Starts HTTP server runtime in background thread.
     */
    void run() override;

    /**
     * @brief Stops HTTP server runtime and joins background thread.
     */
    void close() override;

    /**
     * @brief Injects publish callback from generic MQTT client.
     * @param callback Publish callback.
     */
    void setPublishCallback(PublishCallback callback) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Loads client runtime config from INI document.
 * @param iniDocument Parsed INI data.
 * @param configOutput Target config output.
 * @param errorOutput Error text output on failure.
 * @return True when mapping succeeded, otherwise false.
 */
[[nodiscard]] bool tryLoadHttpMqttInterfaceClientConfigFromIni(
    const IniDocument& iniDocument,
    HttpMqttInterfaceClientConfig& configOutput,
    std::string& errorOutput);

} // namespace yaha
