#pragma once

#include "yaha/message/message.h"
#include "yaha/mqtt_client/mqtt_client.h"

#include <chrono>
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

struct HttpMqttSessionSnapshot {
    std::string clientId{};
    std::string sendToken{};
    std::string receiveToken{};
    bool brokerConnected{false};
};

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

    /**
     * @brief Records client activity for keep-alive expiry.
     *
     * Ping, publish, subscribe and unsubscribe record activity themselves. Callers use this
     * for client-driven receive requests; internal dispatcher polling must not call it.
     *
     * @param token Send or receive token of the session.
     */
    void markActivity(const std::string& token);

    /**
     * @brief Removes sessions whose client stayed silent longer than 1.5 times its keep-alive.
     *
     * Sessions connected without keep-alive never expire. Expired sessions are disconnected
     * from the broker and their tokens become unknown.
     *
     * @param now Current steady-clock time.
     * @return Client identifiers of the expired sessions.
     */
    [[nodiscard]] std::vector<std::string> expireIdleSessions(std::chrono::steady_clock::time_point now);

    [[nodiscard]] bool hasSession(const std::string& token) const;
    [[nodiscard]] bool resolveClientIdByToken(const std::string& token, std::string& clientIdOut) const;
    [[nodiscard]] bool resolveSendTokenByClientId(const std::string& clientId, std::string& tokenOut) const;
    [[nodiscard]] std::vector<HttpMqttSessionSnapshot> listSessions() const;

private:
    struct SessionState;

    [[nodiscard]] std::optional<std::shared_ptr<SessionState>> findSession(const std::string& token) const;
    /**
     * @brief Looks up a session and records client activity on it.
     * @param token Send or receive token of the session.
     * @return Session when found, otherwise empty.
     */
    [[nodiscard]] std::optional<std::shared_ptr<SessionState>> findActiveSession(const std::string& token) const;
    void disconnectExistingSessionForClientId(const std::string& clientId);
    [[nodiscard]] static std::string createToken();

    YahaMqttClient::Config baseConfig_{};
    HttpMqttSessionTransportFactory transportFactory_{};

    mutable std::mutex sessionsMutex_{};
    std::unordered_map<std::string, std::shared_ptr<SessionState>> sessionsBySendToken_{};
    std::unordered_map<std::string, std::shared_ptr<SessionState>> sessionsByReceiveToken_{};
    std::unordered_map<std::string, std::string> sendTokenByClientId_{};
};

} // namespace yaha
