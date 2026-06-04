#include "yaha/http_mqtt_interface_client/http_mqtt_session_manager.h"

#include "yaha/mqtt_client/broker_transport.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace yaha {

namespace {

constexpr std::uint8_t k_subscribe_reject_code{128U};
constexpr std::uint8_t k_unsubscribe_no_subscription{17U};

} // namespace

struct HttpMqttSessionManager::SessionState {
    YahaMqttClient::Transport transport{};
    std::string clientId{};
    std::string sendToken{};
    std::string receiveToken{};
    std::mutex operationMutex{};
};

HttpMqttSessionManager::HttpMqttSessionManager(
    YahaMqttClient::Config baseConfig,
    HttpMqttSessionTransportFactory transportFactory)
    : baseConfig_(std::move(baseConfig))
    , transportFactory_(std::move(transportFactory)) {
    if (!transportFactory_) {
        transportFactory_ = []() {
            return makeBrokerTransport();
        };
    }
}

bool HttpMqttSessionManager::connect(
    const HttpMqttSessionConnectRequest& request,
    HttpMqttSessionConnectTokens& tokensOut,
    std::string& errorOut) {
    errorOut.clear();
    if (request.clientId.empty()) {
        errorOut = "clientId is required";
        return false;
    }

    auto session = std::make_shared<SessionState>();
    session->transport = transportFactory_();
    session->clientId = request.clientId;
    session->sendToken = createToken();
    session->receiveToken = createToken();

    YahaMqttClient::Config sessionConfig = baseConfig_;
    sessionConfig.clientId = request.clientId;
    if (request.brokerHost.has_value()) {
        sessionConfig.brokerHost = *request.brokerHost;
    }
    if (request.brokerPort.has_value()) {
        sessionConfig.brokerPort = *request.brokerPort;
    }
    if (request.keepAliveSeconds.has_value()) {
        sessionConfig.keepAliveInterval = std::chrono::seconds(*request.keepAliveSeconds);
    }

    try {
        if (!session->transport.connect || !session->transport.connect(sessionConfig)) {
            errorOut = "broker connect failed";
            return false;
        }
    } catch (const std::exception& exceptionValue) {
        errorOut = exceptionValue.what();
        return false;
    } catch (...) {
        errorOut = "unknown broker connect failure";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock{sessionsMutex_};
        sessionsBySendToken_[session->sendToken] = session;
        sessionsByReceiveToken_[session->receiveToken] = session;
        sendTokenByClientId_[session->clientId] = session->sendToken;
    }

    tokensOut.sendToken = session->sendToken;
    tokensOut.receiveToken = session->receiveToken;
    return true;
}

bool HttpMqttSessionManager::disconnect(const std::string& token, std::string& errorOut) {
    errorOut.clear();
    const auto maybeSession = findSession(token);
    if (!maybeSession.has_value()) {
        errorOut = "unknown token";
        return false;
    }

    const std::shared_ptr<SessionState>& session = *maybeSession;
    {
        std::lock_guard<std::mutex> operationLock{session->operationMutex};
        try {
            if (session->transport.disconnect) {
                session->transport.disconnect();
            }
        } catch (const std::exception& exceptionValue) {
            errorOut = exceptionValue.what();
            return false;
        } catch (...) {
            errorOut = "unknown broker disconnect failure";
            return false;
        }
    }

    std::lock_guard<std::mutex> lock{sessionsMutex_};
    sessionsBySendToken_.erase(session->sendToken);
    sessionsByReceiveToken_.erase(session->receiveToken);
    if (const auto byClientId = sendTokenByClientId_.find(session->clientId);
        byClientId != sendTokenByClientId_.end() && byClientId->second == session->sendToken) {
        sendTokenByClientId_.erase(byClientId);
    }
    return true;
}

bool HttpMqttSessionManager::subscribe(
    const std::string& token,
    const std::map<std::string, Qos>& topics,
    std::vector<std::uint8_t>& resultCodes,
    std::string& errorOut) {
    errorOut.clear();
    resultCodes.clear();

    const auto maybeSession = findSession(token);
    if (!maybeSession.has_value()) {
        errorOut = "unknown token";
        return false;
    }

    const std::shared_ptr<SessionState>& session = *maybeSession;
    std::lock_guard<std::mutex> operationLock{session->operationMutex};

    try {
        for (const auto& [topic, qos] : topics) {
            if (!session->transport.subscribe || !session->transport.subscribe(topic, qos)) {
                resultCodes.push_back(k_subscribe_reject_code);
                continue;
            }
            resultCodes.push_back(static_cast<std::uint8_t>(qos));
        }
    } catch (const std::exception& exceptionValue) {
        errorOut = exceptionValue.what();
        return false;
    } catch (...) {
        errorOut = "unknown broker subscribe failure";
        return false;
    }

    return true;
}

bool HttpMqttSessionManager::unsubscribe(
    const std::string& token,
    const std::map<std::string, Qos>& topics,
    std::vector<std::uint8_t>& resultCodes,
    std::string& errorOut) {
    errorOut.clear();
    resultCodes.clear();

    const auto maybeSession = findSession(token);
    if (!maybeSession.has_value()) {
        errorOut = "unknown token";
        return false;
    }

    const std::shared_ptr<SessionState>& session = *maybeSession;
    std::lock_guard<std::mutex> operationLock{session->operationMutex};

    try {
        for (const auto& [topic, _qos] : topics) {
            if (!session->transport.unsubscribe || !session->transport.unsubscribe(topic)) {
                resultCodes.push_back(k_unsubscribe_no_subscription);
                continue;
            }
            resultCodes.push_back(0U);
        }
    } catch (const std::exception& exceptionValue) {
        errorOut = exceptionValue.what();
        return false;
    } catch (...) {
        errorOut = "unknown broker unsubscribe failure";
        return false;
    }

    return true;
}

bool HttpMqttSessionManager::publish(const std::string& token, const Message& message, std::string& errorOut) {
    errorOut.clear();
    const auto maybeSession = findSession(token);
    if (!maybeSession.has_value()) {
        errorOut = "unknown token";
        return false;
    }

    const std::shared_ptr<SessionState>& session = *maybeSession;
    std::lock_guard<std::mutex> operationLock{session->operationMutex};

    try {
        if (!session->transport.publish) {
            errorOut = "publish transport not configured";
            return false;
        }
        session->transport.publish(message);
    } catch (const std::exception& exceptionValue) {
        errorOut = exceptionValue.what();
        return false;
    } catch (...) {
        errorOut = "unknown broker publish failure";
        return false;
    }

    return true;
}

bool HttpMqttSessionManager::receive(
    const std::string& token,
    std::optional<Message>& messageOut,
    std::string& errorOut) {
    errorOut.clear();
    messageOut = std::nullopt;

    const auto maybeSession = findSession(token);
    if (!maybeSession.has_value()) {
        errorOut = "unknown token";
        return false;
    }

    const std::shared_ptr<SessionState>& session = *maybeSession;
    std::lock_guard<std::mutex> operationLock{session->operationMutex};

    try {
        if (!session->transport.pollIncoming) {
            errorOut = "receive transport not configured";
            return false;
        }
        messageOut = session->transport.pollIncoming();
    } catch (const std::exception& exceptionValue) {
        errorOut = exceptionValue.what();
        return false;
    } catch (...) {
        errorOut = "unknown broker receive failure";
        return false;
    }

    return true;
}

bool HttpMqttSessionManager::ping(const std::string& token, std::string& errorOut) {
    errorOut.clear();
    const auto maybeSession = findSession(token);
    if (!maybeSession.has_value()) {
        errorOut = "unknown token";
        return false;
    }

    const std::shared_ptr<SessionState>& session = *maybeSession;
    std::lock_guard<std::mutex> operationLock{session->operationMutex};

    try {
        if (session->transport.ping) {
            session->transport.ping();
        }
        if (session->transport.isConnected && !session->transport.isConnected()) {
            errorOut = "session disconnected";
            return false;
        }
    } catch (const std::exception& exceptionValue) {
        errorOut = exceptionValue.what();
        return false;
    } catch (...) {
        errorOut = "unknown broker ping failure";
        return false;
    }

    return true;
}

bool HttpMqttSessionManager::hasSession(const std::string& token) const {
    return findSession(token).has_value();
}

bool HttpMqttSessionManager::resolveSendTokenByClientId(
    const std::string& clientId,
    std::string& tokenOut) const {
    tokenOut.clear();
    if (clientId.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock{sessionsMutex_};
    const auto byClientId = sendTokenByClientId_.find(clientId);
    if (byClientId == sendTokenByClientId_.end()) {
        return false;
    }
    if (sessionsBySendToken_.find(byClientId->second) == sessionsBySendToken_.end()) {
        return false;
    }

    tokenOut = byClientId->second;
    return true;
}

std::optional<std::shared_ptr<HttpMqttSessionManager::SessionState>> HttpMqttSessionManager::findSession(
    const std::string& token) const {
    std::lock_guard<std::mutex> lock{sessionsMutex_};
    if (const auto bySend = sessionsBySendToken_.find(token); bySend != sessionsBySendToken_.end()) {
        return bySend->second;
    }
    if (const auto byReceive = sessionsByReceiveToken_.find(token); byReceive != sessionsByReceiveToken_.end()) {
        return byReceive->second;
    }
    return std::nullopt;
}

std::string HttpMqttSessionManager::createToken() {
    static std::atomic<std::uint64_t> sequence{0U};
    const std::uint64_t nowValue = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::uint64_t seqValue = ++sequence;

    std::ostringstream output{};
    output << "tok-" << std::hex << nowValue << '-' << seqValue;
    return output.str();
}

} // namespace yaha
