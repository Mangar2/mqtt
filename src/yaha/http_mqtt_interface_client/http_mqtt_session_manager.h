#pragma once

#include "yaha/message/message.h"
#include "yaha/mqtt_client/mqtt_client.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace yaha {

struct HttpMqttSessionConnectRequest {
    std::string clientId{};
    std::optional<std::string> brokerHost{};
    std::optional<std::uint16_t> brokerPort{};
    std::optional<std::uint32_t> keepAliveSeconds{};
};

struct HttpMqttSessionConnectTokens {
    std::string sendToken{};
    std::string receiveToken{};
};

using HttpMqttSessionTransportFactory = std::function<YahaMqttClient::Transport()>;

class HttpMqttSessionManager {
public:
    HttpMqttSessionManager(
        YahaMqttClient::Config baseConfig,
        HttpMqttSessionTransportFactory transportFactory);

    [[nodiscard]] bool connect(
        const HttpMqttSessionConnectRequest& request,
        HttpMqttSessionConnectTokens& tokensOut,
        std::string& errorOut);

    [[nodiscard]] bool disconnect(const std::string& token, std::string& errorOut);

    [[nodiscard]] bool subscribe(
        const std::string& token,
        const std::map<std::string, Qos>& topics,
        std::vector<std::uint8_t>& resultCodes,
        std::string& errorOut);

    [[nodiscard]] bool unsubscribe(
        const std::string& token,
        const std::map<std::string, Qos>& topics,
        std::vector<std::uint8_t>& resultCodes,
        std::string& errorOut);

    [[nodiscard]] bool publish(const std::string& token, const Message& message, std::string& errorOut);

    [[nodiscard]] bool receive(
        const std::string& token,
        std::optional<Message>& messageOut,
        std::string& errorOut);

    [[nodiscard]] bool ping(const std::string& token, std::string& errorOut);

    [[nodiscard]] bool hasSession(const std::string& token) const;

private:
    struct SessionState;

    [[nodiscard]] std::optional<std::shared_ptr<SessionState>> findSession(const std::string& token) const;
    [[nodiscard]] static std::string createToken();

    YahaMqttClient::Config baseConfig_{};
    HttpMqttSessionTransportFactory transportFactory_{};

    mutable std::mutex sessionsMutex_{};
    std::unordered_map<std::string, std::shared_ptr<SessionState>> sessionsBySendToken_{};
    std::unordered_map<std::string, std::shared_ptr<SessionState>> sessionsByReceiveToken_{};
};

} // namespace yaha
