#include "yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.h"

#include "yaha/error_handling/yaha_error.h"
#include "yaha/http_mqtt_interface/http_mqtt_interface_operations.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <httplib.h>

#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace yaha {

namespace {

constexpr std::string_view k_httpSection{"httpMqttInterface"};
constexpr std::string_view k_listenerHostKey{"listenerHost"};
constexpr std::string_view k_listenerPortKey{"listenerPort"};
constexpr std::string_view k_enablePublishPhpAliasKey{"enablePublishPhpAlias"};
constexpr std::string_view k_useLegacyPhpResponseKey{"useLegacyPhpResponse"};
constexpr std::string_view k_healthEndpoint{"/health"};
constexpr std::string_view k_publishEndpoint{"/publish"};
constexpr std::string_view k_publishPhpEndpoint{"/publish.php"};
constexpr std::string_view k_pubrelEndpoint{"/pubrel"};
constexpr int k_httpStatusOk{200};
constexpr int k_httpStatusNoContent{204};
constexpr int k_httpStatusInternalServerError{500};
constexpr std::string_view k_publishCorsMethods{"POST, PUT, OPTIONS"};
constexpr std::string_view k_publishCorsHeaders{"Content-Type, Authorization, X-Requested-With"};
constexpr const char* k_error_code_broker_publish_failed{"HTTP_MQTT_BROKER_PUBLISH_FAILED"};
constexpr const char* k_error_code_listener_start_failed{"HTTP_MQTT_LISTENER_START_FAILED"};

[[nodiscard]] std::string messageValueToText(const Value& messageValue) {
    if (std::holds_alternative<std::string>(messageValue)) {
        return std::get<std::string>(messageValue);
    }

    std::ostringstream outputStream{};
    outputStream << std::get<double>(messageValue);
    return outputStream.str();
}

[[nodiscard]] std::string qosToText(const Qos qosValue) {
    switch (qosValue) {
        case Qos::AtMostOnce:
            return "0";
        case Qos::AtLeastOnce:
            return "1";
        case Qos::ExactlyOnce:
            return "2";
    }

    return "0";
}

[[nodiscard]] std::string describeBrokerForwardMessage(
    const Message& message,
    const HttpMqttRequestData& mappedRequest) {
    std::ostringstream output{};
    output << " topic=" << message.topic()
           << " qos=" << qosToText(message.qos())
           << " retain=" << (message.retain() ? "1" : "0")
           << " dup=" << (message.dup() ? "1" : "0");

    const auto packetIdIterator = mappedRequest.headers.find("packetid");
    if (packetIdIterator != mappedRequest.headers.end()) {
        output << " packetid=" << packetIdIterator->second;
    }

    output << " value=" << messageValueToText(message.value());
    return output.str();
}

[[nodiscard]] bool isBrokerNoAckError(const std::string_view errorText) {
    return errorText.find("timed out waiting for PUBACK") != std::string_view::npos ||
           errorText.find("timed out waiting for PUBREC") != std::string_view::npos ||
           errorText.find("timed out waiting for PUBCOMP") != std::string_view::npos;
}

void logBrokerForwardPublishAck(const Message& message, const HttpMqttRequestData& mappedRequest) {
    std::cout << "http_mqtt_interface_client[out] broker_publish_ack"
              << describeBrokerForwardMessage(message, mappedRequest)
              << '\n' << std::flush;
}

void logBrokerForwardPublishError(
    const Message& message,
    const HttpMqttRequestData& mappedRequest,
    const std::string_view errorText) {
    std::cout << "http_mqtt_interface_client[error] broker_publish_failed"
              << describeBrokerForwardMessage(message, mappedRequest)
              << " error=" << errorText;
    if (isBrokerNoAckError(errorText)) {
        std::cout << " detail=message_was_sent_but_broker_reported_no_ack";
    }
    std::cout << '\n' << std::flush;
}

void logCompatibilityRequestFailure(const std::string_view endpoint, const std::string_view errorText) {
    std::cerr << "http_mqtt_interface_client[error] publish_request_failed"
              << " endpoint=" << endpoint
              << " error=" << errorText
              << '\n' << std::flush;
}

void logCompatibilityInternalResultFailure(const std::string_view endpoint, const HttpMqttResult& result) {
    std::ostringstream errorText{};
    errorText << "compatibility_result_status=" << result.statusCode;
    if (!result.payload.empty()) {
        errorText << " payload=" << result.payload;
    }
    logCompatibilityRequestFailure(endpoint, errorText.str());
}

[[nodiscard]] HttpMqttResult makeCompatibilityInternalErrorResult() {
    HttpMqttResult result{};
    result.statusCode = k_httpStatusInternalServerError;
    result.headers["content-type"] = "application/json";
    result.payload = "{\"error\":\"internal_error\"}";
    return result;
}

void applyHttpMqttCorsHeaders(httplib::Response& response, const bool includeMaxAge) {
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Methods", std::string{k_publishCorsMethods});
    response.set_header("Access-Control-Allow-Headers", std::string{k_publishCorsHeaders});
    if (includeMaxAge) {
        response.set_header("Access-Control-Max-Age", "86400");
    }
}

void applyHttpMqttResult(const HttpMqttResult& result, httplib::Response& response) {
    applyHttpMqttCorsHeaders(response, false);
    response.status = result.statusCode;
    for (const auto& [headerName, headerValue] : result.headers) {
        response.set_header(headerName, headerValue);
    }
    response.set_content(result.payload, "application/json");
}

void logIncomingPublishRequest(const httplib::Request& request, const std::string_view endpoint) {
    std::cout << "http_mqtt_interface_client[in] method=" << request.method
              << " endpoint=" << endpoint;

    const auto versionIterator = request.headers.find("version");
    if (versionIterator != request.headers.end()) {
        std::cout << " version=" << versionIterator->second;
    }

    std::cout << '\n' << std::flush;
}

HttpMqttHeaders collectHeaders(const httplib::Request& request) {
    HttpMqttHeaders headers{};
    for (const auto& [headerName, headerValue] : request.headers) {
        headers[headerName] = headerValue;
    }

    return headers;
}

HttpMqttHeaders collectFields(const httplib::Request& request) {
    HttpMqttHeaders fields{};
    for (const auto& [fieldName, fieldValue] : request.params) {
        fields[fieldName] = fieldValue;
    }

    return fields;
}

std::string resolveCompatibilityToken(const httplib::Request& request, const HttpMqttHeaders& fields) {
    const auto tokenHeaderIterator = request.headers.find("token");
    if (tokenHeaderIterator != request.headers.end()) {
        return tokenHeaderIterator->second;
    }

    const auto fieldIterator = fields.find("token");
    if (fieldIterator != fields.end()) {
        return fieldIterator->second;
    }

    return "";
}

} // namespace

struct HttpMqttInterfaceClientComponent::Impl {
    explicit Impl(HttpMqttInterfaceClientConfig configInput)
        : config(std::move(configInput))
        , interfaces(makeHttpMqttInterfacesV1())
        , compatibilityConfig{
            .enablePublishPhpAlias = config.enablePublishPhpAlias,
            .responseMode = config.useLegacyPhpResponse
                ? HttpMqttPublishCompatibilityResponseMode::LegacyPhp
                : HttpMqttPublishCompatibilityResponseMode::Native,
        } {}

    HttpMqttInterfaceClientConfig config{};
    HttpMqttInterfaces interfaces;
    HttpMqttPublishCompatibilityConfig compatibilityConfig{};
    httplib::Server server{};

    std::mutex publishCallbackMutex{};
    PublishCallback publishCallback{};

    std::mutex lifecycleMutex{};
    std::condition_variable startupCondition{};
    bool startupResultReady{false};
    bool startupSucceeded{false};
    std::string startupError{};
    bool stopRequested{false};
    bool running{false};
    std::thread serverThread{};
};

HttpMqttInterfaceClientComponent::HttpMqttInterfaceClientComponent(HttpMqttInterfaceClientConfig configInput)
    : impl_(std::make_unique<Impl>(std::move(configInput))) {
    impl_->server.Get(k_healthEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        response.status = k_httpStatusOk;
        response.set_content("ok", "text/plain");
    });

    impl_->server.Put(k_publishEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          logIncomingPublishRequest(request, k_publishEndpoint);
                          try {
                              applyHttpMqttResult(impl_->interfaces.onPublish(collectHeaders(request)), response);
                          } catch (const std::exception& exceptionValue) {
                              logCompatibilityRequestFailure(k_publishEndpoint, exceptionValue.what());
                              applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                          } catch (...) {
                              logCompatibilityRequestFailure(k_publishEndpoint, "unknown publish request error");
                              applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                          }
                      });

    impl_->server.Put(k_pubrelEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          try {
                              applyHttpMqttResult(impl_->interfaces.onPubrel(collectHeaders(request)), response);
                          } catch (const std::exception& exceptionValue) {
                              logCompatibilityRequestFailure(k_pubrelEndpoint, exceptionValue.what());
                              applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                          } catch (...) {
                              logCompatibilityRequestFailure(k_pubrelEndpoint, "unknown publish request error");
                              applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                          }
                      });

    const auto handleCompatibilityRequest = [this](
                                                const httplib::Request& request,
                                                httplib::Response& response,
                                                const std::string_view endpoint) {
        logIncomingPublishRequest(request, endpoint);
        try {
            const HttpMqttHeaders fields = collectFields(request);
            const HttpMqttPublishCompatibilityRequest compatibilityRequest{
                .method = request.method,
                .endpoint = std::string{endpoint},
                .headers = collectHeaders(request),
                .fields = fields,
                .body = request.body,
                .token = resolveCompatibilityToken(request, fields),
            };

            const HttpMqttResult compatibilityResult = handlePublishCompatibilityRequest(
                impl_->interfaces,
                compatibilityRequest,
                impl_->compatibilityConfig,
                [this](const HttpMqttRequestData& downstreamRequest, const Message& mappedMessage) {
                    PublishResult publishResult{};
                    {
                        std::lock_guard<std::mutex> lock{impl_->publishCallbackMutex};
                        publishResult = impl_->publishCallback(mappedMessage);
                    }

                    if (!publishResult.success) {
                        const std::string publishReason = publishResult.reason.empty()
                            ? "broker publish callback failed"
                            : publishResult.reason;
                        logBrokerForwardPublishError(mappedMessage, downstreamRequest, publishReason);
                        throw YahaError{
                            k_error_code_broker_publish_failed,
                            publishReason,
                            "broker publish failed",
                        };
                    }

                    logBrokerForwardPublishAck(mappedMessage, downstreamRequest);
                    return impl_->interfaces.onPublish(downstreamRequest.headers);
                });

            if (compatibilityResult.statusCode >= k_httpStatusInternalServerError) {
                logCompatibilityInternalResultFailure(endpoint, compatibilityResult);
            }
            applyHttpMqttResult(compatibilityResult, response);
        } catch (const std::exception& exceptionValue) {
            logCompatibilityRequestFailure(endpoint, exceptionValue.what());
            applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
        } catch (...) {
            logCompatibilityRequestFailure(endpoint, "unknown publish request error");
            applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
        }
    };

    impl_->server.Post(k_publishEndpoint.data(),
                       [handleCompatibilityRequest](const httplib::Request& request, httplib::Response& response) {
                           handleCompatibilityRequest(request, response, k_publishEndpoint);
                       });

    impl_->server.Post(k_publishPhpEndpoint.data(),
                       [handleCompatibilityRequest](const httplib::Request& request, httplib::Response& response) {
                           handleCompatibilityRequest(request, response, k_publishPhpEndpoint);
                       });

    impl_->server.Options(k_publishEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_publishPhpEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_pubrelEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });
}

HttpMqttInterfaceClientComponent::~HttpMqttInterfaceClientComponent() {
    close();
}

SubscriptionMap HttpMqttInterfaceClientComponent::getSubscriptions() const {
    return {};
}

void HttpMqttInterfaceClientComponent::handleMessage(const Message& /*message*/) {
    // No inbound topic handling required for this HTTP->MQTT forwarding component.
}

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
}

void HttpMqttInterfaceClientComponent::close() {
    {
        std::lock_guard<std::mutex> lock{impl_->lifecycleMutex};
        impl_->stopRequested = true;
    }

    impl_->server.stop();

    if (impl_->serverThread.joinable()) {
        impl_->serverThread.join();
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

    const auto [maybePort, portError] = iniDocument.readUnsigned(
        k_httpSection,
        k_listenerPortKey,
        1U,
        65535U);
    if (!portError.empty()) {
        errorOutput = portError;
        return false;
    }
    if (maybePort.has_value()) {
        configOutput.listenerPort = static_cast<std::uint16_t>(*maybePort);
    }

    const auto [maybePublishPhpAlias, publishPhpAliasError] = iniDocument.readBool(
        k_httpSection,
        k_enablePublishPhpAliasKey);
    if (!publishPhpAliasError.empty()) {
        errorOutput = publishPhpAliasError;
        return false;
    }
    if (maybePublishPhpAlias.has_value()) {
        configOutput.enablePublishPhpAlias = *maybePublishPhpAlias;
    }

    const auto [maybeLegacyResponse, legacyResponseError] = iniDocument.readBool(
        k_httpSection,
        k_useLegacyPhpResponseKey);
    if (!legacyResponseError.empty()) {
        errorOutput = legacyResponseError;
        return false;
    }
    if (maybeLegacyResponse.has_value()) {
        configOutput.useLegacyPhpResponse = *maybeLegacyResponse;
    }

    if (!tryLoadMqttClientConfigFromIni(iniDocument, configOutput.mqttConfig, errorOutput)) {
        return false;
    }

    return true;
}

} // namespace yaha
