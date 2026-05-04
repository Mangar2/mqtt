#pragma once

/**
 * @file http_mqtt_interface_dispatcher.h
 * @brief Version dispatcher and interface facade for HTTP MQTT operations.
 */

#include "yaha/http_mqtt_interface/http_mqtt_interface_contracts.h"
#include "yaha/message/message.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yaha {

/**
 * @brief Connect result token tuple.
 */
struct HttpMqttConnectTokens {
    std::string send{};      ///< Send token.
    std::string receive{};   ///< Receive token.
};

/**
 * @brief Connect result payload shape.
 */
struct HttpMqttConnectResult {
    std::optional<std::uint8_t> mqttCode{};        ///< Optional MQTT connect result code.
    std::uint8_t present{0U};                      ///< Session present flag.
    HttpMqttConnectTokens token{};                 ///< Session tokens.
};

/**
 * @brief Connect request options.
 */
struct HttpMqttConnectOptions {
    std::optional<Qos> qos{};                          ///< Optional requested qos.
    std::optional<std::string> clientId{};             ///< Optional client identifier.
    std::optional<std::string> version{};              ///< Optional explicit version.
    std::optional<std::string> host{};                 ///< Optional callback host.
    std::optional<std::uint16_t> port{};               ///< Optional callback port.
    bool clean{true};                                  ///< Clean session flag.
    std::optional<std::uint32_t> keepAlive{};          ///< Optional keep alive value.
    std::optional<std::string> password{};             ///< Optional password.
    std::optional<std::string> user{};                 ///< Optional user name.
    std::optional<Message> will{};                     ///< Optional will message.
};

/**
 * @brief Publish request options.
 */
struct HttpMqttPublishOptions {
    std::string token{};                           ///< Session token.
    Message message{"", std::string{}};          ///< MQTT message.
    std::optional<bool> dup{};                     ///< Optional dup flag.
    std::optional<std::uint16_t> packetId{};       ///< Optional packet id.
};

/**
 * @brief Pubrel request options.
 */
struct HttpMqttPubrelOptions {
    std::string token{};                       ///< Session token.
    std::optional<std::uint16_t> packetId{};   ///< Optional packet id.
};

/**
 * @brief Subscribe return codes.
 */
using HttpMqttSubscribeResult = std::vector<std::uint8_t>;

/**
 * @brief Unsubscribe return codes.
 */
using HttpMqttUnsubscribeResult = std::vector<std::uint8_t>;

/**
 * @brief Publish request handler signature.
 */
using HttpMqttPublishRequestHandler = std::function<HttpMqttRequestData(const HttpMqttPublishOptions&)>;

/**
 * @brief Publish response handler signature.
 */
using HttpMqttPublishResponseHandler = std::function<HttpMqttResult(const HttpMqttHeaders&)>;

/**
 * @brief Pubrel request handler signature.
 */
using HttpMqttPubrelRequestHandler = std::function<HttpMqttRequestData(const HttpMqttPubrelOptions&)>;

/**
 * @brief Pubrel response handler signature.
 */
using HttpMqttPubrelResponseHandler = std::function<HttpMqttResult(const HttpMqttHeaders&)>;

/**
 * @brief Subscribe request handler signature.
 */
using HttpMqttSubscribeRequestHandler = std::function<HttpMqttRequestData(
    const HttpMqttTopics&,
    const std::string&,
    std::uint16_t)>;

/**
 * @brief Subscribe response handler signature.
 */
using HttpMqttSubscribeResponseHandler = std::function<HttpMqttResult(
    const HttpMqttHeaders&,
    const HttpMqttSubscribeResult&)>;

/**
 * @brief Unsubscribe request handler signature.
 */
using HttpMqttUnsubscribeRequestHandler = std::function<HttpMqttRequestData(
    const HttpMqttTopics&,
    const std::string&,
    std::uint16_t)>;

/**
 * @brief Unsubscribe response handler signature.
 */
using HttpMqttUnsubscribeResponseHandler = std::function<HttpMqttResult(
    const HttpMqttHeaders&,
    const HttpMqttUnsubscribeResult&)>;

/**
 * @brief Connect request handler signature.
 */
using HttpMqttConnectRequestHandler = std::function<HttpMqttRequestData(const HttpMqttConnectOptions&)>;

/**
 * @brief Connect response handler signature.
 */
using HttpMqttConnectResponseHandler = std::function<HttpMqttResult(const HttpMqttConnectResult&)>;

/**
 * @brief Disconnect request handler signature.
 */
using HttpMqttDisconnectRequestHandler = std::function<HttpMqttRequestData(const std::string&)>;

/**
 * @brief Disconnect response handler signature.
 */
using HttpMqttDisconnectResponseHandler = std::function<HttpMqttResult()>;

/**
 * @brief Versioned handler map for publish requests.
 */
using HttpMqttPublishRequestHandlerMap = std::map<std::string, HttpMqttPublishRequestHandler>;

/**
 * @brief Versioned handler map for publish responses.
 */
using HttpMqttPublishResponseHandlerMap = std::map<std::string, HttpMqttPublishResponseHandler>;

/**
 * @brief Versioned handler map for pubrel requests.
 */
using HttpMqttPubrelRequestHandlerMap = std::map<std::string, HttpMqttPubrelRequestHandler>;

/**
 * @brief Versioned handler map for pubrel responses.
 */
using HttpMqttPubrelResponseHandlerMap = std::map<std::string, HttpMqttPubrelResponseHandler>;

/**
 * @brief Versioned handler map for subscribe requests.
 */
using HttpMqttSubscribeRequestHandlerMap = std::map<std::string, HttpMqttSubscribeRequestHandler>;

/**
 * @brief Versioned handler map for subscribe responses.
 */
using HttpMqttSubscribeResponseHandlerMap = std::map<std::string, HttpMqttSubscribeResponseHandler>;

/**
 * @brief Versioned handler map for unsubscribe requests.
 */
using HttpMqttUnsubscribeRequestHandlerMap = std::map<std::string, HttpMqttUnsubscribeRequestHandler>;

/**
 * @brief Versioned handler map for unsubscribe responses.
 */
using HttpMqttUnsubscribeResponseHandlerMap = std::map<std::string, HttpMqttUnsubscribeResponseHandler>;

/**
 * @brief Versioned handler map for connect requests.
 */
using HttpMqttConnectRequestHandlerMap = std::map<std::string, HttpMqttConnectRequestHandler>;

/**
 * @brief Versioned handler map for connect responses.
 */
using HttpMqttConnectResponseHandlerMap = std::map<std::string, HttpMqttConnectResponseHandler>;

/**
 * @brief Versioned handler map for disconnect requests.
 */
using HttpMqttDisconnectRequestHandlerMap = std::map<std::string, HttpMqttDisconnectRequestHandler>;

/**
 * @brief Versioned handler map for disconnect responses.
 */
using HttpMqttDisconnectResponseHandlerMap = std::map<std::string, HttpMqttDisconnectResponseHandler>;

/**
 * @brief All versioned handler maps wired into interface facade.
 */
struct HttpMqttInterfaceHandlerRegistry {
    HttpMqttPublishRequestHandlerMap publishRequests{};           ///< Publish request builders.
    HttpMqttPublishResponseHandlerMap publishResponses{};         ///< Publish response handlers.
    HttpMqttPubrelRequestHandlerMap pubrelRequests{};             ///< Pubrel request builders.
    HttpMqttPubrelResponseHandlerMap pubrelResponses{};           ///< Pubrel response handlers.
    HttpMqttSubscribeRequestHandlerMap subscribeRequests{};       ///< Subscribe request builders.
    HttpMqttSubscribeResponseHandlerMap subscribeResponses{};     ///< Subscribe response handlers.
    HttpMqttUnsubscribeRequestHandlerMap unsubscribeRequests{};   ///< Unsubscribe request builders.
    HttpMqttUnsubscribeResponseHandlerMap unsubscribeResponses{}; ///< Unsubscribe response handlers.
    HttpMqttConnectRequestHandlerMap connectRequests{};           ///< Connect request builders.
    HttpMqttConnectResponseHandlerMap connectResponses{};         ///< Connect response handlers.
    HttpMqttDisconnectRequestHandlerMap disconnectRequests{};     ///< Disconnect request builders.
    HttpMqttDisconnectResponseHandlerMap disconnectResponses{};   ///< Disconnect response handlers.
};

/**
 * @brief Resolves version from headers and validates it against onPublish handlers.
 * @param headersInput Incoming headers.
 * @param onPublishHandlers Versioned onPublish handlers.
 * @return Resolved version string.
 * @throws std::runtime_error when version is undefined in onPublish handlers.
 */
[[nodiscard]] std::string getVersion(
    const HttpMqttHeaders& headersInput,
    const HttpMqttPublishResponseHandlerMap& onPublishHandlers);

/**
 * @brief Top-level HTTP MQTT interfaces facade with version dispatch.
 */
class HttpMqttInterfaces {
public:
    /**
     * @brief Builds facade from versioned handler registry.
     * @param registryInput Versioned handlers.
     */
    explicit HttpMqttInterfaces(HttpMqttInterfaceHandlerRegistry registryInput);

    /**
     * @brief Dispatches publish request builder by explicit version.
     * @param version Version key.
     * @param options Publish options.
     * @return Request envelope.
     */
    [[nodiscard]] HttpMqttRequestData publish(
        std::string_view version,
        const HttpMqttPublishOptions& options) const;

    /**
     * @brief Dispatches onPublish handler by resolved header version.
     * @param headersInput Incoming headers.
     * @return Response envelope.
     */
    [[nodiscard]] HttpMqttResult onPublish(const HttpMqttHeaders& headersInput) const;

    /**
     * @brief Dispatches pubrel request builder by explicit version.
     * @param version Version key.
     * @param options Pubrel options.
     * @return Request envelope.
     */
    [[nodiscard]] HttpMqttRequestData pubrel(
        std::string_view version,
        const HttpMqttPubrelOptions& options) const;

    /**
     * @brief Dispatches onPubrel handler by resolved header version.
     * @param headersInput Incoming headers.
     * @return Response envelope.
     */
    [[nodiscard]] HttpMqttResult onPubrel(const HttpMqttHeaders& headersInput) const;

    /**
     * @brief Dispatches subscribe request builder by explicit version.
     * @param version Version key.
     * @param topics Topic/qos map.
     * @param clientId Client identifier.
     * @param packetId Packet id.
     * @return Request envelope.
     */
    [[nodiscard]] HttpMqttRequestData subscribe(
        std::string_view version,
        const HttpMqttTopics& topics,
        const std::string& clientId,
        std::uint16_t packetId) const;

    /**
     * @brief Dispatches onSubscribe handler by resolved header version.
     * @param headersInput Incoming headers.
     * @param result Subscribe result codes.
     * @return Response envelope.
     */
    [[nodiscard]] HttpMqttResult onSubscribe(
        const HttpMqttHeaders& headersInput,
        const HttpMqttSubscribeResult& result) const;

    /**
     * @brief Dispatches unsubscribe request builder by explicit version.
     * @param version Version key.
     * @param topics Topic/qos map.
     * @param clientId Client identifier.
     * @param packetId Packet id.
     * @return Request envelope.
     */
    [[nodiscard]] HttpMqttRequestData unsubscribe(
        std::string_view version,
        const HttpMqttTopics& topics,
        const std::string& clientId,
        std::uint16_t packetId) const;

    /**
     * @brief Dispatches onUnsubscribe handler by resolved header version.
     * @param headersInput Incoming headers.
     * @param result Unsubscribe result codes.
     * @return Response envelope.
     */
    [[nodiscard]] HttpMqttResult onUnsubscribe(
        const HttpMqttHeaders& headersInput,
        const HttpMqttUnsubscribeResult& result) const;

    /**
     * @brief Dispatches connect request builder by explicit version.
     * @param version Version key.
     * @param options Connect options.
     * @return Request envelope.
     */
    [[nodiscard]] HttpMqttRequestData connect(
        std::string_view version,
        const HttpMqttConnectOptions& options) const;

    /**
     * @brief Dispatches onConnect handler by resolved header version.
     * @param headersInput Incoming headers.
     * @param payload Connect result payload.
     * @return Response envelope.
     */
    [[nodiscard]] HttpMqttResult onConnect(
        const HttpMqttHeaders& headersInput,
        const HttpMqttConnectResult& payload) const;

    /**
     * @brief Dispatches disconnect request builder by explicit version.
     * @param version Version key.
     * @param clientId Client identifier.
     * @return Request envelope.
     */
    [[nodiscard]] HttpMqttRequestData disconnect(
        std::string_view version,
        const std::string& clientId) const;

    /**
     * @brief Dispatches onDisconnect handler by resolved header version.
     * @param headersInput Incoming headers.
     * @return Response envelope.
     */
    [[nodiscard]] HttpMqttResult onDisconnect(const HttpMqttHeaders& headersInput) const;

private:
    HttpMqttInterfaceHandlerRegistry registry_{};
};

} // namespace yaha
