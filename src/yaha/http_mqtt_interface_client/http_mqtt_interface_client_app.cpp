#include "yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.h"

#include "yaha/error_handling/yaha_error.h"
#include "yaha/http_mqtt_interface/http_mqtt_interface_operations.h"
#include "yaha/message/message_log_service.h"
#include "yaha/message/message_payload_codec.h"
#include "yaha/mqtt_client/mqtt_client_config.h"
#include "yaha/mqtt_client/broker_transport.h"
#include "json/json_value.h"

#include <httplib.h>

#include <cstdint>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
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
constexpr std::string_view k_connectEndpoint{"/connect"};
constexpr std::string_view k_disconnectEndpoint{"/disconnect"};
constexpr std::string_view k_subscribeEndpoint{"/subscribe"};
constexpr std::string_view k_unsubscribeEndpoint{"/unsubscribe"};
constexpr std::string_view k_pingEndpoint{"/pingreq"};
constexpr std::string_view k_receiveEndpoint{"/receive"};
constexpr int k_httpStatusOk{200};
constexpr int k_httpStatusNoContent{204};
constexpr int k_httpStatusBadRequest{400};
constexpr int k_httpStatusInternalServerError{500};
constexpr int k_uint16_max{65535};
constexpr long long k_uint32_max{4294967295LL};
constexpr std::string_view k_publishCorsMethods{"POST, PUT, OPTIONS"};
constexpr std::string_view k_publishCorsHeaders{"Content-Type, Authorization, X-Requested-With"};
constexpr const char* k_error_code_broker_publish_failed{"HTTP_MQTT_BROKER_PUBLISH_FAILED"};
constexpr const char* k_error_code_listener_start_failed{"HTTP_MQTT_LISTENER_START_FAILED"};

[[nodiscard]] std::optional<std::string> buildBrokerForwardLogLine(const Message& message) {
    constexpr MessageLogConfig k_log_config{
        .enableIncoming = false,
        .enableOutgoing = true,
        .includeReasonChain = true,
    };

    return buildMessageLogLine(
        "http_mqtt_interface_client",
        MessageLogDirection::Outgoing,
        message,
        k_log_config);
}

[[nodiscard]] bool isBrokerNoAckError(const std::string_view errorText) {
    return errorText.find("timed out waiting for PUBACK") != std::string_view::npos ||
           errorText.find("timed out waiting for PUBREC") != std::string_view::npos ||
           errorText.find("timed out waiting for PUBCOMP") != std::string_view::npos;
}

void logBrokerForwardPublishAck(const Message& message) {
    if (const auto line = buildBrokerForwardLogLine(message); line.has_value()) {
        std::cout << *line << " event=broker_publish_ack" << '\n' << std::flush;
    }
}

void logBrokerForwardPublishError(
    const Message& message,
    const std::string_view errorText) {
    if (const auto line = buildBrokerForwardLogLine(message); line.has_value()) {
        std::cout << *line
                  << " event=broker_publish_failed"
                  << " error=\"" << escapeJsonString(errorText) << '\"';
        if (isBrokerNoAckError(errorText)) {
            std::cout << " detail=message_was_sent_but_broker_reported_no_ack";
        }
        std::cout << '\n' << std::flush;
    }
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
    result.payload = R"({"error":"internal_error"})";
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

[[nodiscard]] std::optional<mqtt::json::JsonValue> tryParseJsonBody(const httplib::Request& request) {
    if (request.body.empty()) {
        return std::nullopt;
    }
    return mqtt::json::JsonValue::try_parse(request.body);
}

[[nodiscard]] std::optional<std::string> tryReadStringField(
    const mqtt::json::JsonValue& value,
    const std::string_view fieldName) {
    if (!value.is_object() || !value.contains(fieldName)) {
        return std::nullopt;
    }
    const auto& fieldValue = value.at(fieldName);
    if (!fieldValue.is_string()) {
        return std::nullopt;
    }
    return fieldValue.as_string();
}

[[nodiscard]] std::optional<std::uint16_t> tryReadUInt16Field(
    const mqtt::json::JsonValue& value,
    const std::string_view fieldName) {
    if (!value.is_object() || !value.contains(fieldName)) {
        return std::nullopt;
    }
    const auto& fieldValue = value.at(fieldName);
    if (!fieldValue.is_number()) {
        return std::nullopt;
    }
    const auto numberValue = static_cast<int>(fieldValue.as_number());
    if (numberValue < 0 || numberValue > k_uint16_max) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(numberValue);
}

[[nodiscard]] std::optional<std::uint32_t> tryReadUInt32Field(
    const mqtt::json::JsonValue& value,
    const std::string_view fieldName) {
    if (!value.is_object() || !value.contains(fieldName)) {
        return std::nullopt;
    }
    const auto& fieldValue = value.at(fieldName);
    if (!fieldValue.is_number()) {
        return std::nullopt;
    }
    const auto numberValue = static_cast<long long>(fieldValue.as_number());
    if (numberValue < 0 || numberValue > k_uint32_max) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(numberValue);
}

[[nodiscard]] std::optional<int> tryReadIntField(
    const mqtt::json::JsonValue& value,
    const std::string_view fieldName) {
    if (!value.is_object() || !value.contains(fieldName)) {
        return std::nullopt;
    }
    const auto& fieldValue = value.at(fieldName);
    if (!fieldValue.is_number()) {
        return std::nullopt;
    }
    return static_cast<int>(fieldValue.as_number());
}

[[nodiscard]] std::string resolveToken(
    const httplib::Request& request,
    const HttpMqttHeaders& fields,
    const std::optional<mqtt::json::JsonValue>& jsonBody) {
    std::string tokenFromHeaders = resolveCompatibilityToken(request, fields);
    if (!tokenFromHeaders.empty()) {
        return tokenFromHeaders;
    }

    if (jsonBody.has_value()) {
        const auto tokenFromJson = tryReadStringField(*jsonBody, "token");
        if (tokenFromJson.has_value()) {
            return *tokenFromJson;
        }
    }

    return "";
}

[[nodiscard]] std::map<std::string, Qos> parseTopicsObject(
    const std::optional<mqtt::json::JsonValue>& jsonBody,
    std::string& errorText) {
    std::map<std::string, Qos> topics{};
    errorText.clear();

    if (!jsonBody.has_value() || !jsonBody->is_object() || !jsonBody->contains("topics")) {
        errorText = "missing topics object";
        return topics;
    }

    const auto& topicsValue = jsonBody->at("topics");
    if (!topicsValue.is_object()) {
        errorText = "topics must be object";
        return topics;
    }

    for (const auto& [topic, qosValue] : topicsValue.as_object()) {
        if (!qosValue.is_number()) {
            errorText = "topic qos must be numeric";
            topics.clear();
            return topics;
        }

        const int qosNumber = static_cast<int>(qosValue.as_number());
        if (qosNumber < 0 || qosNumber > 2) {
            errorText = "topic qos out of range";
            topics.clear();
            return topics;
        }

        topics[topic] = static_cast<Qos>(qosNumber);
    }

    return topics;
}

[[nodiscard]] mqtt::json::JsonValue messageValueToJson(const Value& value) {
    if (std::holds_alternative<double>(value)) {
        return mqtt::json::JsonValue{std::get<double>(value)};
    }
    return mqtt::json::JsonValue{std::get<std::string>(value)};
}

[[nodiscard]] std::string buildReceivePayload(const Message& message) {
    mqtt::json::JsonValue payload = mqtt::json::JsonValue::object();
    payload["message"] = mqtt::json::JsonValue::object();
    payload["message"]["topic"] = message.topic();
    payload["message"]["value"] = messageValueToJson(message.value());
    payload["message"]["qos"] = static_cast<double>(static_cast<int>(message.qos()));
    payload["message"]["retain"] = message.retain();
    payload["message"]["dup"] = message.dup();

    payload["message"]["reason"] = mqtt::json::JsonValue::array();
    for (const auto& reasonEntry : message.reason()) {
        mqtt::json::JsonValue reasonObject = mqtt::json::JsonValue::object();
        reasonObject["message"] = reasonEntry.message;
        reasonObject["timestamp"] = reasonEntry.timestamp;
        payload["message"]["reason"].push_back(std::move(reasonObject));
    }

    return payload.stringify();
}

[[nodiscard]] HttpMqttResult makeJsonErrorResult(const int statusCode, const std::string_view errorCode) {
    HttpMqttResult result{};
    result.statusCode = statusCode;
    result.headers["content-type"] = "application/json; charset=UTF-8";
    result.headers["version"] = "1.0";
    result.payload = std::string{R"({"error":")"} + std::string{errorCode} + R"("})";
    return result;
}

[[nodiscard]] HttpMqttResult makeNoContentResult(const std::string_view packetName = "") {
    HttpMqttResult result{};
    result.statusCode = k_httpStatusNoContent;
    result.headers["content-type"] = "application/json; charset=UTF-8";
    result.headers["version"] = "1.0";
    if (!packetName.empty()) {
        result.headers["packet"] = std::string{packetName};
    }
    result.payload.clear();
    return result;
}

void logConfigFallbackWarning(
    const std::string_view serviceName,
    const std::string_view sectionName,
    const std::string_view keyName,
    const std::string& rawValue,
    const std::string& defaultValue,
    const std::string& reasonText) {
    std::cerr << serviceName << "[warn] config_fallback"
              << " section=" << sectionName
              << " key=" << keyName
              << " value='" << rawValue << "'"
              << " default='" << defaultValue << "'"
              << " reason='" << reasonText << "'"
              << '\n' << std::flush;
}

} // namespace

struct HttpMqttInterfaceClientComponent::Impl {
    explicit Impl(
        HttpMqttInterfaceClientConfig configInput,
        HttpMqttSessionTransportFactory sessionTransportFactory)
        : config(std::move(configInput))
        , interfaces(makeHttpMqttInterfacesV1())
        , sessionManager(config.mqttConfig, std::move(sessionTransportFactory))
        , compatibilityConfig{
            .enablePublishPhpAlias = config.enablePublishPhpAlias,
            .responseMode = config.useLegacyPhpResponse
                ? HttpMqttPublishCompatibilityResponseMode::LegacyPhp
                : HttpMqttPublishCompatibilityResponseMode::Native,
        } {}

    HttpMqttInterfaceClientConfig config{};
    HttpMqttInterfaces interfaces;
    HttpMqttSessionManager sessionManager;
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
    : HttpMqttInterfaceClientComponent(
        std::move(configInput),
        []() {
            return makeBrokerTransport();
        }) {}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
HttpMqttInterfaceClientComponent::HttpMqttInterfaceClientComponent(
    HttpMqttInterfaceClientConfig configInput,
    HttpMqttSessionTransportFactory sessionTransportFactory)
    : impl_(std::make_unique<Impl>(std::move(configInput), std::move(sessionTransportFactory))) {
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

    impl_->server.Put(k_connectEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          const auto clientId = jsonBody.has_value()
                              ? tryReadStringField(*jsonBody, "clientId")
                              : std::nullopt;
                          if (!clientId.has_value() || clientId->empty()) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_client_id"), response);
                              return;
                          }

                          HttpMqttSessionConnectRequest connectRequest{};
                          connectRequest.clientId = *clientId;
                          if (jsonBody.has_value()) {
                              connectRequest.brokerHost = tryReadStringField(*jsonBody, "host");
                              connectRequest.brokerPort = tryReadUInt16Field(*jsonBody, "port");
                              connectRequest.keepAliveSeconds = tryReadUInt32Field(*jsonBody, "keepAlive");
                          }

                          HttpMqttSessionConnectTokens tokens{};
                          std::string connectError{};
                          if (!impl_->sessionManager.connect(connectRequest, tokens, connectError)) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "connect_failed"), response);
                              return;
                          }

                          HttpMqttConnectResult connectResult{};
                          connectResult.present = 0U;
                          connectResult.token.send = tokens.sendToken;
                          connectResult.token.receive = tokens.receiveToken;

                          HttpMqttHeaders headers = collectHeaders(request);
                          if (headers.find("version") == headers.end()) {
                              headers["version"] = "1.0";
                          }

                          try {
                              applyHttpMqttResult(impl_->interfaces.onConnect(headers, connectResult), response);
                          } catch (...) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "connect_response_failed"), response);
                          }
                      });

    impl_->server.Put(k_subscribeEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          std::string topicsError{};
                          const std::map<std::string, Qos> topics = parseTopicsObject(jsonBody, topicsError);
                          if (!topicsError.empty()) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_topics"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          if (token.empty()) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_token"), response);
                              return;
                          }

                          HttpMqttHeaders headers = collectHeaders(request);
                          if (headers.find("version") == headers.end()) {
                              headers["version"] = "1.0";
                          }
                          if (jsonBody.has_value()) {
                              if (const auto packetId = tryReadIntField(*jsonBody, "packetid"); packetId.has_value()) {
                                  headers["packetid"] = std::to_string(*packetId);
                              }
                          }

                          std::vector<std::uint8_t> subscribeResult{};
                          std::string subscribeError{};
                          if (!impl_->sessionManager.subscribe(token, topics, subscribeResult, subscribeError)) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "subscribe_failed"), response);
                              return;
                          }

                          try {
                              applyHttpMqttResult(impl_->interfaces.onSubscribe(headers, subscribeResult), response);
                          } catch (...) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "subscribe_response_failed"), response);
                          }
                      });

    impl_->server.Put(k_unsubscribeEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          std::string topicsError{};
                          const std::map<std::string, Qos> topics = parseTopicsObject(jsonBody, topicsError);
                          if (!topicsError.empty()) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_topics"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          if (token.empty()) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_token"), response);
                              return;
                          }

                          HttpMqttHeaders headers = collectHeaders(request);
                          if (headers.find("version") == headers.end()) {
                              headers["version"] = "1.0";
                          }
                          if (jsonBody.has_value()) {
                              if (const auto packetId = tryReadIntField(*jsonBody, "packetid"); packetId.has_value()) {
                                  headers["packetid"] = std::to_string(*packetId);
                              }
                          }

                          std::vector<std::uint8_t> unsubscribeResult{};
                          std::string unsubscribeError{};
                          if (!impl_->sessionManager.unsubscribe(token, topics, unsubscribeResult, unsubscribeError)) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "unsubscribe_failed"), response);
                              return;
                          }

                          try {
                              applyHttpMqttResult(impl_->interfaces.onUnsubscribe(headers, unsubscribeResult), response);
                          } catch (...) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "unsubscribe_response_failed"), response);
                          }
                      });

    impl_->server.Put(k_disconnectEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          if (token.empty()) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_token"), response);
                              return;
                          }

                          std::string disconnectError{};
                          if (!impl_->sessionManager.disconnect(token, disconnectError)) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "disconnect_failed"), response);
                              return;
                          }

                          HttpMqttHeaders headers = collectHeaders(request);
                          if (headers.find("version") == headers.end()) {
                              headers["version"] = "1.0";
                          }

                          try {
                              applyHttpMqttResult(impl_->interfaces.onDisconnect(headers), response);
                          } catch (...) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "disconnect_response_failed"), response);
                          }
                      });

    impl_->server.Put(k_pingEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          if (token.empty()) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_token"), response);
                              return;
                          }

                          std::string pingError{};
                          if (!impl_->sessionManager.ping(token, pingError)) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "ping_failed"), response);
                              return;
                          }

                          applyHttpMqttResult(makeNoContentResult("pingresp"), response);
                      });

    impl_->server.Put(k_receiveEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          if (token.empty()) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_token"), response);
                              return;
                          }

                          std::optional<Message> receivedMessage{};
                          std::string receiveError{};
                          if (!impl_->sessionManager.receive(token, receivedMessage, receiveError)) {
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "receive_failed"), response);
                              return;
                          }

                          if (!receivedMessage.has_value()) {
                              applyHttpMqttResult(makeNoContentResult(), response);
                              return;
                          }

                          HttpMqttResult receiveResult{};
                          receiveResult.statusCode = k_httpStatusOk;
                          receiveResult.headers["content-type"] = "application/json; charset=UTF-8";
                          receiveResult.headers["version"] = "1.0";
                          receiveResult.headers["packet"] = "publish";
                          receiveResult.payload = buildReceivePayload(*receivedMessage);
                          applyHttpMqttResult(receiveResult, response);
                      });

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
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
                    std::string sessionToken{};
                    if (const auto parsedPayload = mqtt::json::JsonValue::try_parse(downstreamRequest.payload);
                        parsedPayload.has_value()) {
                        if (const auto tokenField = tryReadStringField(*parsedPayload, "token"); tokenField.has_value()) {
                            sessionToken = *tokenField;
                        }
                    }

                    if (!sessionToken.empty() && impl_->sessionManager.hasSession(sessionToken)) {
                        std::string sessionError{};
                        if (!impl_->sessionManager.publish(sessionToken, mappedMessage, sessionError)) {
                            const std::string publishReason = sessionError.empty()
                                ? "broker publish callback failed"
                                : sessionError;
                            logBrokerForwardPublishError(mappedMessage, publishReason);
                            throw YahaError{
                                k_error_code_broker_publish_failed,
                                publishReason,
                                "broker publish failed",
                            };
                        }
                    } else {
                        PublishResult publishResult{};
                        {
                            std::lock_guard<std::mutex> lock{impl_->publishCallbackMutex};
                            publishResult = impl_->publishCallback(mappedMessage);
                        }

                        if (!publishResult.success) {
                            const std::string publishReason = publishResult.reason.empty()
                                ? "broker publish callback failed"
                                : publishResult.reason;
                            logBrokerForwardPublishError(mappedMessage, publishReason);
                            throw YahaError{
                                k_error_code_broker_publish_failed,
                                publishReason,
                                "broker publish failed",
                            };
                        }
                    }

                    logBrokerForwardPublishAck(mappedMessage);
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

    impl_->server.Options(k_connectEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_disconnectEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_subscribeEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_unsubscribeEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_pingEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_receiveEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
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
        const std::string rawValue = iniDocument.lastValue(k_httpSection, k_listenerPortKey).value_or("<missing>");
        logConfigFallbackWarning(
            "http_mqtt_interface_client",
            k_httpSection,
            k_listenerPortKey,
            rawValue,
            std::to_string(configOutput.listenerPort),
            portError);
    }
    if (maybePort.has_value()) {
        configOutput.listenerPort = static_cast<std::uint16_t>(*maybePort);
    }

    const auto [maybePublishPhpAlias, publishPhpAliasError] = iniDocument.readBool(
        k_httpSection,
        k_enablePublishPhpAliasKey);
    if (!publishPhpAliasError.empty()) {
        const std::string rawValue =
            iniDocument.lastValue(k_httpSection, k_enablePublishPhpAliasKey).value_or("<missing>");
        logConfigFallbackWarning(
            "http_mqtt_interface_client",
            k_httpSection,
            k_enablePublishPhpAliasKey,
            rawValue,
            configOutput.enablePublishPhpAlias ? "true" : "false",
            publishPhpAliasError);
    }
    if (maybePublishPhpAlias.has_value()) {
        configOutput.enablePublishPhpAlias = *maybePublishPhpAlias;
    }

    const auto [maybeLegacyResponse, legacyResponseError] = iniDocument.readBool(
        k_httpSection,
        k_useLegacyPhpResponseKey);
    if (!legacyResponseError.empty()) {
        const std::string rawValue =
            iniDocument.lastValue(k_httpSection, k_useLegacyPhpResponseKey).value_or("<missing>");
        logConfigFallbackWarning(
            "http_mqtt_interface_client",
            k_httpSection,
            k_useLegacyPhpResponseKey,
            rawValue,
            configOutput.useLegacyPhpResponse ? "true" : "false",
            legacyResponseError);
    }
    if (maybeLegacyResponse.has_value()) {
        configOutput.useLegacyPhpResponse = *maybeLegacyResponse;
    }

    std::string mqttErrorMessage{};
    if (!tryLoadMqttClientConfigFromIni(iniDocument, configOutput.mqttConfig, mqttErrorMessage)) {
        logConfigFallbackWarning(
            "http_mqtt_interface_client",
            "mqtt",
            "*",
            "<composite>",
            "defaults",
            mqttErrorMessage);
    }

    errorOutput.clear();
    return true;
}

} // namespace yaha
