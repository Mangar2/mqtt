#include "yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.h"

#include "yaha/http_mqtt_interface/http_mqtt_interface_operations.h"
#include "yaha/mqtt_client/broker_transport.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <httplib.h>

#include <atomic>
#include <csignal>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <string>
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

std::atomic<httplib::Server*> g_activeServer{nullptr};

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

HttpMqttResult handleCompatibilityPublish(
    const HttpMqttInterfaces& interfaces,
    const httplib::Request& request,
    const HttpMqttPublishCompatibilityConfig& compatibilityConfig,
    const std::string_view endpoint,
    const HttpMqttInterfacePublishToBroker& publishToBroker) {
    const HttpMqttHeaders fields = collectFields(request);
    const HttpMqttPublishCompatibilityRequest compatibilityRequest{
        .method = request.method,
        .endpoint = std::string{endpoint},
        .headers = collectHeaders(request),
        .fields = fields,
        .body = request.body,
        .token = resolveCompatibilityToken(request, fields),
    };

    return handlePublishCompatibilityRequest(
        interfaces,
        compatibilityRequest,
        compatibilityConfig,
        [&interfaces, &publishToBroker](
            const HttpMqttRequestData& downstreamRequest,
            const Message& mappedMessage) {
            try {
                publishToBroker(mappedMessage);
                logBrokerForwardPublishAck(mappedMessage, downstreamRequest);
            } catch (const std::exception& exception) {
                logBrokerForwardPublishError(mappedMessage, downstreamRequest, exception.what());
                throw;
            } catch (...) {
                logBrokerForwardPublishError(mappedMessage, downstreamRequest, "unknown broker publish error");
                throw;
            }
            return interfaces.onPublish(downstreamRequest.headers);
        });
}

void stopActiveServerSignalHandler(int /*signalValue*/) {
    httplib::Server* server = g_activeServer.load();
    if (server != nullptr) {
        server->stop();
    }
}

} // namespace

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

int runHttpMqttInterfaceClient(const HttpMqttInterfaceClientConfig& configInput) {
    return runHttpMqttInterfaceClient(configInput, makeBrokerTransport());
}

int runHttpMqttInterfaceClient(
    const HttpMqttInterfaceClientConfig& configInput,
    YahaMqttClient::Transport brokerTransport) {
    std::mutex brokerTransportStateMutex{};
    bool brokerConnected = false;

    const auto publishToBroker = [&](const Message& message) {
        std::lock_guard<std::mutex> lock{brokerTransportStateMutex};
        if (!brokerConnected) {
            if (!brokerTransport.connect(configInput.mqttConfig)) {
                throw std::runtime_error{"failed to connect broker transport"};
            }
            brokerConnected = true;
        }

        try {
            brokerTransport.publish(message);
        } catch (...) {
            brokerConnected = false;
            throw;
        }
    };

    const int exitCode = runHttpMqttInterfaceClient(configInput, publishToBroker);

    {
        std::lock_guard<std::mutex> lock{brokerTransportStateMutex};
        if (brokerConnected) {
            try {
                brokerTransport.disconnect();
            } catch (const std::exception& exception) {
                std::cerr << "http_mqtt_interface_client[error] broker_disconnect_failed"
                          << " error=" << exception.what()
                          << '\n' << std::flush;
            } catch (...) {
                std::cerr << "http_mqtt_interface_client[error] broker_disconnect_failed"
                          << " error=unknown"
                          << '\n' << std::flush;
            }
            brokerConnected = false;
        }
    }

    return exitCode;
}

int runHttpMqttInterfaceClient(
    const HttpMqttInterfaceClientConfig& configInput,
    const HttpMqttInterfacePublishToBroker& publishToBroker) {
    HttpMqttInterfaces interfaces = makeHttpMqttInterfacesV1();
    const HttpMqttPublishCompatibilityConfig compatibilityConfig{
        .enablePublishPhpAlias = configInput.enablePublishPhpAlias,
        .responseMode = configInput.useLegacyPhpResponse
            ? HttpMqttPublishCompatibilityResponseMode::LegacyPhp
            : HttpMqttPublishCompatibilityResponseMode::Native,
    };

    httplib::Server server{};

    server.Get(k_healthEndpoint.data(), [](const httplib::Request& /*request*/, httplib::Response& response) {
        response.status = k_httpStatusOk;
        response.set_content("ok", "text/plain");
    });

    server.Put(k_publishEndpoint.data(),
               [&interfaces](const httplib::Request& request, httplib::Response& response) {
                   logIncomingPublishRequest(request, k_publishEndpoint);
                   try {
                       applyHttpMqttResult(interfaces.onPublish(collectHeaders(request)), response);
                   } catch (const std::exception& exception) {
                       logCompatibilityRequestFailure(k_publishEndpoint, exception.what());
                       applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                   } catch (...) {
                       logCompatibilityRequestFailure(k_publishEndpoint, "unknown publish request error");
                       applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                   }
               });

    server.Put(k_pubrelEndpoint.data(),
               [&interfaces](const httplib::Request& request, httplib::Response& response) {
                   try {
                       applyHttpMqttResult(interfaces.onPubrel(collectHeaders(request)), response);
                   } catch (const std::exception& exception) {
                       logCompatibilityRequestFailure(k_pubrelEndpoint, exception.what());
                       applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                   } catch (...) {
                       logCompatibilityRequestFailure(k_pubrelEndpoint, "unknown publish request error");
                       applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                   }
               });

    server.Post(k_publishEndpoint.data(),
                [&interfaces, &compatibilityConfig, &publishToBroker](
                    const httplib::Request& request,
                    httplib::Response& response) {
                    logIncomingPublishRequest(request, k_publishEndpoint);
                    try {
                        const HttpMqttResult compatibilityResult = handleCompatibilityPublish(
                            interfaces,
                            request,
                            compatibilityConfig,
                            k_publishEndpoint,
                            publishToBroker);
                        if (compatibilityResult.statusCode >= k_httpStatusInternalServerError) {
                            logCompatibilityInternalResultFailure(k_publishEndpoint, compatibilityResult);
                        }
                        applyHttpMqttResult(compatibilityResult, response);
                    } catch (const std::exception& exception) {
                        logCompatibilityRequestFailure(k_publishEndpoint, exception.what());
                        applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                    } catch (...) {
                        logCompatibilityRequestFailure(k_publishEndpoint, "unknown publish request error");
                        applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                    }
                });

    server.Post(k_publishPhpEndpoint.data(),
                [&interfaces, &compatibilityConfig, &publishToBroker](
                    const httplib::Request& request,
                    httplib::Response& response) {
                    logIncomingPublishRequest(request, k_publishPhpEndpoint);
                    try {
                        const HttpMqttResult compatibilityResult = handleCompatibilityPublish(
                            interfaces,
                            request,
                            compatibilityConfig,
                            k_publishPhpEndpoint,
                            publishToBroker);
                        if (compatibilityResult.statusCode >= k_httpStatusInternalServerError) {
                            logCompatibilityInternalResultFailure(k_publishPhpEndpoint, compatibilityResult);
                        }
                        applyHttpMqttResult(compatibilityResult, response);
                    } catch (const std::exception& exception) {
                        logCompatibilityRequestFailure(k_publishPhpEndpoint, exception.what());
                        applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                    } catch (...) {
                        logCompatibilityRequestFailure(k_publishPhpEndpoint, "unknown publish request error");
                        applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                    }
                });

    server.Options(k_publishEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    server.Options(k_publishPhpEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    server.Options(k_pubrelEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    std::signal(SIGINT, stopActiveServerSignalHandler);
    std::signal(SIGTERM, stopActiveServerSignalHandler);

    g_activeServer.store(&server);

    std::cout << "yahahttpmqttinterfaceclient\n";
    std::cout << "  listener: " << configInput.listenerHost << ':' << configInput.listenerPort << '\n';
    std::cout << "  mqtt: " << configInput.mqttConfig.brokerHost << ':' << configInput.mqttConfig.brokerPort
              << " clientId=" << configInput.mqttConfig.clientId << '\n';
    std::cout << "  compatibility: publish.php=" << (configInput.enablePublishPhpAlias ? "on" : "off")
              << " legacyResponse=" << (configInput.useLegacyPhpResponse ? "on" : "off") << '\n';
    std::cout << "  signal: waiting for SIGINT/SIGTERM\n";
    std::cout << std::flush;

    const bool listenSuccess = server.listen(configInput.listenerHost, configInput.listenerPort);
    g_activeServer.store(nullptr);

    if (!listenSuccess) {
        std::cerr << "Failed to start HTTP listener on " << configInput.listenerHost << ':'
                  << configInput.listenerPort << '\n';
        return 1;
    }

    return 0;
}

} // namespace yaha
