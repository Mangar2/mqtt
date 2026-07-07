#include "yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.h"

#include "yaha/error_handling/yaha_error.h"
#include "yaha/http_mqtt_interface_client/internal/http_mqtt_interface_client_app_internal.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace yaha {
using namespace http_mqtt_interface_client_internal;


HttpMqttInterfaceClientComponent::~HttpMqttInterfaceClientComponent() {
    close();
}

SubscriptionMap HttpMqttInterfaceClientComponent::getSubscriptions() const {
    return {};
}

void HttpMqttInterfaceClientComponent::handleMessage(const Message& /*message*/) {
    // No inbound topic handling required for this HTTP->MQTT forwarding component.
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void HttpMqttInterfaceClientComponent::run() {
    std::unique_lock<std::mutex> lifecycleLock{impl_->lifecycleMutex};
    if (impl_->running) {
        return;
    }

    impl_->startupResultReady = false;
    impl_->startupSucceeded = false;
    impl_->startupError.clear();
    impl_->stopRequested = false;

    impl_->serverThread = std::thread([this]() {
        const int boundPort = impl_->server.bind_to_port(impl_->config.listenerHost, impl_->config.listenerPort);
        {
            std::lock_guard<std::mutex> lifecycleLock{impl_->lifecycleMutex};
            impl_->startupResultReady = true;
            if (boundPort <= 0) {
                impl_->startupSucceeded = false;
                impl_->startupError = "failed to bind HTTP listener on " +
                    impl_->config.listenerHost + ":" + std::to_string(impl_->config.listenerPort);
            } else {
                impl_->startupSucceeded = true;
                impl_->running = true;
            }
        }
        impl_->startupCondition.notify_all();

        if (boundPort <= 0) {
            return;
        }

        const bool listenSuccess = impl_->server.listen_after_bind();
        if (!listenSuccess) {
            const bool shouldLogFailure = [&]() {
                std::lock_guard<std::mutex> lifecycleLock{impl_->lifecycleMutex};
                return !impl_->stopRequested;
            }();
            if (shouldLogFailure) {
                std::cerr << "Failed to run HTTP listener on " << impl_->config.listenerHost << ':'
                          << impl_->config.listenerPort << '\n';
            }
        }

        std::lock_guard<std::mutex> lifecycleLock{impl_->lifecycleMutex};
        impl_->running = false;
    });

    impl_->startupCondition.wait(lifecycleLock, [this]() {
        return impl_->startupResultReady;
    });

    if (!impl_->startupSucceeded) {
        const std::string startupErrorText = impl_->startupError;
        lifecycleLock.unlock();
        if (impl_->serverThread.joinable()) {
            impl_->serverThread.join();
        }
        throw YahaError{
            k_error_code_listener_start_failed,
            startupErrorText,
            "http listener start failed",
        };
    }

    lifecycleLock.unlock();

    {
        std::lock_guard<std::mutex> lock{impl_->legacyListenerMutex};
        impl_->legacyDispatchStopRequested = false;
    }

    impl_->legacyDispatchThread = std::thread([this]() {
        while (true) {
            std::vector<std::tuple<std::string, std::string, LegacyListenerEndpoint>> snapshots{};
            {
                std::lock_guard<std::mutex> lock{impl_->legacyListenerMutex};
                if (impl_->legacyDispatchStopRequested) {
                    break;
                }

                snapshots.reserve(impl_->legacyListenerBySendToken.size());
                for (const auto& [sendToken, endpoint] : impl_->legacyListenerBySendToken) {
                    const auto receiveTokenIt = impl_->receiveTokenBySendToken.find(sendToken);
                    if (receiveTokenIt == impl_->receiveTokenBySendToken.end()) {
                        continue;
                    }
                    snapshots.emplace_back(sendToken, receiveTokenIt->second, endpoint);
                }
            }

            if (snapshots.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds{k_legacy_dispatch_idle_sleep_ms});
                continue;
            }

            for (const auto& snapshot : snapshots) {
                const auto& sendToken = std::get<0>(snapshot);
                const auto& receiveToken = std::get<1>(snapshot);
                const auto& endpoint = std::get<2>(snapshot);

                std::optional<Message> receivedMessage{};
                std::string receiveError{};
                if (!impl_->sessionManager.receive(receiveToken, receivedMessage, receiveError)) {
                    if (!receiveError.empty()) {
                        logHttpMqttError(impl_->config.logErrors, "receive_dispatch", "session receive failed", receiveError);
                    }
                    continue;
                }

                if (!receivedMessage.has_value()) {
                    continue;
                }

                logBrokerIncomingMessage(
                    impl_->config.logBrokerMessages,
                    impl_->config.logReason,
                    *receivedMessage);

                const bool forwarded = forwardLegacyListenerPublish(
                    endpoint,
                    *receivedMessage,
                    HttpMqttHeaders{});
                std::string resolvedClientId{};
                (void)impl_->sessionManager.resolveClientIdByToken(sendToken, resolvedClientId);
                logListenerForwardResult(
                    impl_->config.logEvents,
                    impl_->config.logErrors,
                    resolvedClientId,
                    sendToken,
                    *receivedMessage,
                    forwarded);
            }
        }
    });

    {
        std::lock_guard<std::mutex> lock{impl_->lifecycleMutex};
        impl_->connectedClientsReportStopRequested = false;
    }
    impl_->connectedClientsReportThread = std::thread([this]() {
        while (true) {
            const std::uint32_t intervalSeconds =
                std::max<std::uint32_t>(1U, impl_->config.connectedClientsReportIntervalSeconds);
            const std::uint32_t intervalMs = intervalSeconds * 1000U;
            std::uint32_t waitedMs = 0U;
            while (waitedMs < intervalMs) {
                {
                    std::lock_guard<std::mutex> lock{impl_->lifecycleMutex};
                    if (impl_->connectedClientsReportStopRequested) {
                        return;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{k_connected_clients_report_sleep_step_ms});
                waitedMs += static_cast<std::uint32_t>(k_connected_clients_report_sleep_step_ms);
            }

            const auto sessions = impl_->sessionManager.listSessions();
            logConnectedClientsReport(impl_->config.logEvents, sessions);
        }
    });
}

void HttpMqttInterfaceClientComponent::close() {
    {
        std::lock_guard<std::mutex> lock{impl_->lifecycleMutex};
        impl_->stopRequested = true;
    }

    {
        std::lock_guard<std::mutex> lock{impl_->legacyListenerMutex};
        impl_->legacyDispatchStopRequested = true;
    }

    impl_->server.stop();

    if (impl_->serverThread.joinable()) {
        impl_->serverThread.join();
    }

    if (impl_->legacyDispatchThread.joinable()) {
        impl_->legacyDispatchThread.join();
    }

    {
        std::lock_guard<std::mutex> lock{impl_->lifecycleMutex};
        impl_->connectedClientsReportStopRequested = true;
    }

    if (impl_->connectedClientsReportThread.joinable()) {
        impl_->connectedClientsReportThread.join();
    }

    std::lock_guard<std::mutex> lock{impl_->lifecycleMutex};
    impl_->running = false;
}

void HttpMqttInterfaceClientComponent::setPublishCallback(PublishCallback callback) {
    std::lock_guard<std::mutex> lock{impl_->publishCallbackMutex};
    impl_->publishCallback = std::move(callback);
}

bool tryLoadHttpMqttInterfaceClientConfigFromIni(
    const IniDocument& iniDocument,
    HttpMqttInterfaceClientConfig& configOutput,
    std::string& errorOutput) {
    errorOutput.clear();

    if (const auto maybeHost = iniDocument.lastValue(k_httpSection, k_listenerHostKey); maybeHost.has_value()) {
        configOutput.listenerHost = *maybeHost;
    }

    const auto maybePort = iniDocument.readUnsigned(
        k_httpSection,
        k_listenerPortKey,
        1U,
        65535U,
        std::to_string(configOutput.listenerPort));
    if (maybePort.has_value()) {
        configOutput.listenerPort = static_cast<std::uint16_t>(*maybePort);
    }

    applyBoolConfigWithFallback(
        iniDocument,
        configOutput.enablePublishPhpAlias,
        k_httpSection,
        k_enablePublishPhpAliasKey);
    applyBoolConfigWithFallback(
        iniDocument,
        configOutput.useLegacyPhpResponse,
        k_httpSection,
        k_useLegacyPhpResponseKey);
    applyBoolConfigWithFallback(
        iniDocument,
        configOutput.logIncomingRequests,
        k_httpSection,
        k_logIncomingRequestsKey);
    applyBoolConfigWithFallback(
        iniDocument,
        configOutput.logEvents,
        k_httpSection,
        k_logEventsKey);
    applyBoolConfigWithFallback(
        iniDocument,
        configOutput.logErrors,
        k_httpSection,
        k_logErrorsKey);
    applyBoolConfigWithFallback(
        iniDocument,
        configOutput.logBrokerMessages,
        k_httpSection,
        k_logBrokerMessagesKey);
    applyBoolConfigWithFallback(
        iniDocument,
        configOutput.logReason,
        k_httpSection,
        k_logReasonKey);
    applyBoolConfigWithFallback(
        iniDocument,
        configOutput.logTracing,
        k_httpSection,
        k_logTracingKey);

    const auto maybeConnectedClientsReportIntervalSeconds = iniDocument.readUnsigned(
        k_httpSection,
        k_connectedClientsReportIntervalSecondsKey,
        1U,
        86400U,
        std::to_string(configOutput.connectedClientsReportIntervalSeconds));
    if (maybeConnectedClientsReportIntervalSeconds.has_value()) {
        configOutput.connectedClientsReportIntervalSeconds = *maybeConnectedClientsReportIntervalSeconds;
    }

    std::string mqttErrorMessage{};
    if (!tryLoadMqttClientConfigFromIni(iniDocument, configOutput.mqttConfig, mqttErrorMessage)) {
        iniDocument.reportFallback("mqtt", "*", "<composite>", "defaults", mqttErrorMessage);
    }

    errorOutput.clear();
    return true;
}

} // namespace yaha
