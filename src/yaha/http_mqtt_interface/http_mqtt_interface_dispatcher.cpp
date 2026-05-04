#include "yaha/http_mqtt_interface/http_mqtt_interface_dispatcher.h"

#include "yaha/http_mqtt_interface/http_mqtt_interface_contracts.h"

#include <format>
#include <stdexcept>

namespace yaha {

namespace {

[[nodiscard]] std::string makeUndefinedVersionMessage(const std::string_view version) {
    return std::format("undefined version {}", version);
}

template <typename HandlerMap>
[[nodiscard]] const typename HandlerMap::mapped_type& resolveHandler(
    const HandlerMap& handlerMap,
    const std::string_view version) {
    const auto iterator = handlerMap.find(std::string{version});
    if (iterator == handlerMap.end()) {
        throw std::runtime_error{makeUndefinedVersionMessage(version)};
    }
    return iterator->second;
}

} // namespace

std::string getVersion(
    const HttpMqttHeaders& headersInput,
    const HttpMqttPublishResponseHandlerMap& onPublishHandlers) {
    const std::string selectedVersion = resolveVersion(headersInput, "0.0");
    if (onPublishHandlers.find(selectedVersion) == onPublishHandlers.end()) {
        throw std::runtime_error{makeUndefinedVersionMessage(selectedVersion)};
    }
    return selectedVersion;
}

HttpMqttInterfaces::HttpMqttInterfaces(HttpMqttInterfaceHandlerRegistry registryInput)
    : registry_(std::move(registryInput)) {}

HttpMqttRequestData HttpMqttInterfaces::publish(
    const std::string_view version,
    const HttpMqttPublishOptions& options) const {
    const auto& handler = resolveHandler(registry_.publishRequests, version);
    return handler(options);
}

HttpMqttResult HttpMqttInterfaces::onPublish(const HttpMqttHeaders& headersInput) const {
    const std::string version = getVersion(headersInput, registry_.publishResponses);
    const auto& handler = resolveHandler(registry_.publishResponses, version);
    return handler(headersInput);
}

HttpMqttRequestData HttpMqttInterfaces::pubrel(
    const std::string_view version,
    const HttpMqttPubrelOptions& options) const {
    const auto& handler = resolveHandler(registry_.pubrelRequests, version);
    return handler(options);
}

HttpMqttResult HttpMqttInterfaces::onPubrel(const HttpMqttHeaders& headersInput) const {
    const std::string version = getVersion(headersInput, registry_.publishResponses);
    const auto& handler = resolveHandler(registry_.pubrelResponses, version);
    return handler(headersInput);
}

HttpMqttRequestData HttpMqttInterfaces::subscribe(
    const std::string_view version,
    const HttpMqttTopics& topics,
    const std::string& clientId,
    const std::uint16_t packetId) const {
    const auto& handler = resolveHandler(registry_.subscribeRequests, version);
    return handler(topics, clientId, packetId);
}

HttpMqttResult HttpMqttInterfaces::onSubscribe(
    const HttpMqttHeaders& headersInput,
    const HttpMqttSubscribeResult& result) const {
    const std::string version = getVersion(headersInput, registry_.publishResponses);
    const auto& handler = resolveHandler(registry_.subscribeResponses, version);
    return handler(headersInput, result);
}

HttpMqttRequestData HttpMqttInterfaces::unsubscribe(
    const std::string_view version,
    const HttpMqttTopics& topics,
    const std::string& clientId,
    const std::uint16_t packetId) const {
    const auto& handler = resolveHandler(registry_.unsubscribeRequests, version);
    return handler(topics, clientId, packetId);
}

HttpMqttResult HttpMqttInterfaces::onUnsubscribe(
    const HttpMqttHeaders& headersInput,
    const HttpMqttUnsubscribeResult& result) const {
    const std::string version = getVersion(headersInput, registry_.publishResponses);
    const auto& handler = resolveHandler(registry_.unsubscribeResponses, version);
    return handler(headersInput, result);
}

HttpMqttRequestData HttpMqttInterfaces::connect(
    const std::string_view version,
    const HttpMqttConnectOptions& options) const {
    const auto& handler = resolveHandler(registry_.connectRequests, version);
    return handler(options);
}

HttpMqttResult HttpMqttInterfaces::onConnect(
    const HttpMqttHeaders& headersInput,
    const HttpMqttConnectResult& payload) const {
    const std::string version = getVersion(headersInput, registry_.publishResponses);
    const auto& handler = resolveHandler(registry_.connectResponses, version);
    return handler(payload);
}

HttpMqttRequestData HttpMqttInterfaces::disconnect(
    const std::string_view version,
    const std::string& clientId) const {
    const auto& handler = resolveHandler(registry_.disconnectRequests, version);
    return handler(clientId);
}

HttpMqttResult HttpMqttInterfaces::onDisconnect(const HttpMqttHeaders& headersInput) const {
    const std::string version = getVersion(headersInput, registry_.publishResponses);
    const auto& handler = resolveHandler(registry_.disconnectResponses, version);
    return handler();
}

} // namespace yaha
