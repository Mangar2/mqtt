#include "yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.h"

#include "yaha/error_handling/yaha_error.h"
#include "yaha/http_mqtt_interface/http_mqtt_interface_operations.h"
#include "yaha/message/message_log_service.h"
#include "yaha/message/message_payload_codec.h"
#include "yaha/mqtt_client/mqtt_client_config.h"
#include "yaha/mqtt_client/broker_transport.h"
#include "json/json_value.h"

#include <httplib.h>

#include <chrono>
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
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yaha {

namespace {

constexpr std::string_view k_httpSection{"httpMqttInterface"};
constexpr std::string_view k_listenerHostKey{"listenerHost"};
constexpr std::string_view k_listenerPortKey{"listenerPort"};
constexpr std::string_view k_enablePublishPhpAliasKey{"enablePublishPhpAlias"};
constexpr std::string_view k_useLegacyPhpResponseKey{"useLegacyPhpResponse"};
constexpr std::string_view k_logIncomingRequestsKey{"logIncomingRequests"};
constexpr std::string_view k_logEventsKey{"logEvents"};
constexpr std::string_view k_logErrorsKey{"logErrors"};
constexpr std::string_view k_logBrokerMessagesKey{"logBrokerMessages"};
constexpr std::string_view k_connectedClientsReportIntervalSecondsKey{"connectedClientsReportIntervalSeconds"};
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
constexpr int k_legacy_listener_timeout_us{300000};
constexpr int k_legacy_dispatch_idle_sleep_ms{25};
constexpr int k_connected_clients_report_sleep_step_ms{250};
constexpr int k_uint16_max{65535};
constexpr long long k_uint32_max{4294967295LL};
constexpr std::string_view k_publishCorsMethods{"POST, PUT, OPTIONS"};
constexpr std::string_view k_publishCorsHeaders{"Content-Type, Authorization, X-Requested-With"};
constexpr const char* k_error_code_broker_publish_failed{"HTTP_MQTT_BROKER_PUBLISH_FAILED"};
constexpr const char* k_error_code_listener_start_failed{"HTTP_MQTT_LISTENER_START_FAILED"};

void logHttpMqttEvent(const bool enabled, const std::string_view eventName, const std::string_view detailText = "") {
    if (!enabled) {
        return;
    }

    std::cout << "http_mqtt_interface_client[event] " << eventName;
    if (!detailText.empty()) {
        std::cout << ' ' << detailText;
    }
    std::cout << '\n' << std::flush;
}

void logHttpMqttError(
    const bool enabled,
    const std::string_view operationName,
    const std::string_view reasonText,
    const std::string_view detailText = "") {
    if (!enabled) {
        return;
    }

    std::cerr << "http_mqtt_interface_client[error] operation=" << operationName
              << " reason=\"" << escapeJsonString(reasonText) << '\"';
    if (!detailText.empty()) {
        std::cerr << " detail=\"" << escapeJsonString(detailText) << '\"';
    }
    std::cerr << '\n' << std::flush;
}

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

void logBrokerForwardPublishAck(const bool enabled, const Message& message) {
    if (!enabled) {
        return;
    }

    if (const auto line = buildBrokerForwardLogLine(message); line.has_value()) {
        std::cout << *line << " event=broker_publish_ack" << '\n' << std::flush;
    }
}

void logBrokerIncomingMessage(const bool enabled, const Message& message) {
    if (!enabled) {
        return;
    }

    constexpr MessageLogConfig k_log_config{
        .enableIncoming = true,
        .enableOutgoing = false,
        .includeReasonChain = true,
    };

    if (const auto line = buildMessageLogLine(
            "http_mqtt_interface_client",
            MessageLogDirection::Incoming,
            message,
            k_log_config);
        line.has_value()) {
        std::cout << *line << " event=broker_message_in" << '\n' << std::flush;
    }
}

void logLegacyForwardResult(
    const bool eventLoggingEnabled,
    const bool errorLoggingEnabled,
    const std::string_view clientId,
    const std::string_view token,
    const Message& message,
    const bool forwarded) {
    std::ostringstream details{};
    if (!clientId.empty()) {
        details << "clientId=" << clientId << ' ';
    }
    if (!token.empty()) {
        details << "token=" << token << ' ';
    }
    details << "topic=" << message.topic()
            << " qos=" << static_cast<int>(message.qos())
            << " forwarded=" << (forwarded ? "true" : "false");
    if (forwarded) {
        logHttpMqttEvent(eventLoggingEnabled, "legacy_listener_forward", details.str());
    } else {
        logHttpMqttError(errorLoggingEnabled, "legacy_listener_forward", "forward failed", details.str());
    }
}

void logConnectedClientsReport(const bool enabled, const std::vector<HttpMqttSessionSnapshot>& sessions) {
    if (!enabled) {
        return;
    }

    std::size_t connectedCount = 0U;
    std::ostringstream clients{};
    bool firstClient = true;
    for (const auto& session : sessions) {
        if (!session.brokerConnected) {
            continue;
        }
        ++connectedCount;
        if (!firstClient) {
            clients << ',';
        }
        firstClient = false;
        clients << session.clientId;
    }

    std::ostringstream details{};
    details << "connected_clients=" << connectedCount;
    if (connectedCount > 0U) {
        details << " client_ids=" << clients.str();
    }
    logHttpMqttEvent(enabled, "connected_clients_report", details.str());
}

void logBrokerForwardPublishError(
    const bool enabled,
    const Message& message,
    const std::string_view errorText) {
    if (!enabled) {
        return;
    }

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

void logBrokerPublishDispatchAttempt(
    const bool enabled,
    const Message& message,
    const std::string_view token,
    const std::string_view dispatchPath) {
    std::ostringstream detail{};
    if (!token.empty()) {
        detail << "token=" << token << ' ';
    }
    detail << "path=" << dispatchPath
           << " topic=" << message.topic()
           << " qos=" << static_cast<int>(message.qos());
    logHttpMqttEvent(enabled, "broker_publish_dispatch", detail.str());
}

void logBrokerPublishDispatchSent(
    const bool enabled,
    const Message& message,
    const std::string_view token,
    const std::string_view dispatchPath) {
    std::ostringstream detail{};
    if (!token.empty()) {
        detail << "token=" << token << ' ';
    }
    detail << "path=" << dispatchPath
           << " topic=" << message.topic()
           << " qos=" << static_cast<int>(message.qos());
    logHttpMqttEvent(enabled, "broker_publish_sent", detail.str());
}

void logBrokerPublishDispatchFailed(
    const bool enabled,
    const Message& message,
    const std::string_view token,
    const std::string_view dispatchPath,
    const std::string_view reasonText) {
    std::ostringstream detail{};
    if (!token.empty()) {
        detail << "token=" << token << ' ';
    }
    detail << "path=" << dispatchPath
           << " topic=" << message.topic()
           << " qos=" << static_cast<int>(message.qos())
           << " error=" << reasonText;
    logHttpMqttError(enabled, "broker_publish", "dispatch failed", detail.str());
}

void logCompatibilityRequestFailure(
    const bool enabled,
    const std::string_view endpoint,
    const std::string_view errorText) {
    if (!enabled) {
        return;
    }

    std::cerr << "http_mqtt_interface_client[error] publish_request_failed"
              << " endpoint=" << endpoint
              << " error=" << errorText
              << '\n' << std::flush;
}

void logCompatibilityInternalResultFailure(
    const bool enabled,
    const std::string_view endpoint,
    const HttpMqttResult& result) {
    if (!enabled) {
        return;
    }

    std::ostringstream errorText{};
    errorText << "compatibility_result_status=" << result.statusCode;
    if (!result.payload.empty()) {
        errorText << " payload=" << result.payload;
    }
    logCompatibilityRequestFailure(enabled, endpoint, errorText.str());
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

void logIncomingPublishRequest(
    const bool enabled,
    const httplib::Request& request,
    const std::string_view endpoint,
    const std::string_view clientId,
    const std::string_view token,
    const std::string_view topic) {
    if (!enabled) {
        return;
    }

    std::cout << "http_mqtt_interface_client[in] method=" << request.method
              << " endpoint=" << endpoint;

    const auto versionIterator = request.headers.find("version");
    if (versionIterator != request.headers.end()) {
        std::cout << " version=" << versionIterator->second;
    }

    std::cout << " body_bytes=" << request.body.size();

    if (!clientId.empty()) {
        std::cout << " clientId=" << clientId;
    }
    if (!token.empty()) {
        std::cout << " token=" << token;
    }
    if (!topic.empty()) {
        std::cout << " topic=" << topic;
    }

    std::cout << '\n' << std::flush;
}

[[nodiscard]] std::string resolveClientIdForRequest(
    const HttpMqttSessionManager& sessionManager,
    const std::optional<mqtt::json::JsonValue>& jsonBody,
    const std::string& token) {
    if (jsonBody.has_value()) {
        if (jsonBody->is_object() && jsonBody->contains("clientId")) {
            const auto& clientIdValue = jsonBody->at("clientId");
            if (clientIdValue.is_string()) {
                return clientIdValue.as_string();
            }
        }
    }

    std::string resolvedClientId{};
    if (!token.empty() && sessionManager.resolveClientIdByToken(token, resolvedClientId)) {
        return resolvedClientId;
    }

    return "";
}

[[nodiscard]] std::string resolveTopicForRequest(
    const HttpMqttHeaders& fields,
    const std::optional<mqtt::json::JsonValue>& jsonBody) {
    if (const auto topicIterator = fields.find("topic"); topicIterator != fields.end()) {
        return topicIterator->second;
    }

    if (jsonBody.has_value()) {
        if (jsonBody->is_object() && jsonBody->contains("topic")) {
            const auto& topicValue = jsonBody->at("topic");
            if (topicValue.is_string()) {
                return topicValue.as_string();
            }
        }
    }

    return "";
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
    std::string& errorText,
    const std::string_view legacyFieldName = "") {
    std::map<std::string, Qos> topics{};
    errorText.clear();

    if (!jsonBody.has_value() || !jsonBody->is_object()) {
        errorText = "missing topics object";
        return topics;
    }

    const mqtt::json::JsonValue* topicsValue = nullptr;
    if (jsonBody->contains("topics")) {
        topicsValue = &jsonBody->at("topics");
    } else if (!legacyFieldName.empty() && jsonBody->contains(legacyFieldName)) {
        topicsValue = &jsonBody->at(legacyFieldName);
    } else {
        errorText = "missing topics object";
        return topics;
    }

    const auto& topicsObject = *topicsValue;
    if (!topicsObject.is_object()) {
        errorText = "topics must be object";
        return topics;
    }

    for (const auto& [topic, qosValue] : topicsObject.as_object()) {
        int qosNumber = -1;
        if (qosValue.is_number()) {
            qosNumber = static_cast<int>(qosValue.as_number());
        } else if (qosValue.is_string()) {
            const std::string qosText = qosValue.as_string();
            if (qosText == "0") {
                qosNumber = 0;
            } else if (qosText == "1") {
                qosNumber = 1;
            } else if (qosText == "2") {
                qosNumber = 2;
            } else {
                errorText = "topic qos string must be one of 0,1,2";
                topics.clear();
                return topics;
            }
        } else {
            errorText = "topic qos must be numeric or numeric-string";
            topics.clear();
            return topics;
        }

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

struct LegacyListenerEndpoint {
    std::string host{};
    std::uint16_t port{0U};
};

[[nodiscard]] bool forwardLegacyListenerPublish(
    const LegacyListenerEndpoint& endpoint,
    const Message& message,
    const HttpMqttHeaders& requestHeaders) {
    if (endpoint.host.empty() || endpoint.port == 0U) {
        return false;
    }

    mqtt::json::JsonValue payload = mqtt::json::JsonValue::object();
    payload["message"] = mqtt::json::JsonValue::object();
    payload["message"]["topic"] = message.topic();
    payload["message"]["value"] = messageValueToJson(message.value());
    payload["message"]["reason"] = mqtt::json::JsonValue::array();
    for (const auto& reasonEntry : message.reason()) {
        mqtt::json::JsonValue reasonObject = mqtt::json::JsonValue::object();
        reasonObject["message"] = reasonEntry.message;
        reasonObject["timestamp"] = reasonEntry.timestamp;
        payload["message"]["reason"].push_back(std::move(reasonObject));
    }

    httplib::Headers headers{};
    headers.emplace("content-type", "application/json; charset=UTF-8");
    headers.emplace("version", "1.0");
    if (const auto qos = requestHeaders.find("qos"); qos != requestHeaders.end()) {
        headers.emplace("qos", qos->second);
    } else {
        headers.emplace("qos", std::to_string(static_cast<int>(message.qos())));
    }
    if (const auto retain = requestHeaders.find("retain"); retain != requestHeaders.end()) {
        headers.emplace("retain", retain->second);
    } else {
        headers.emplace("retain", message.retain() ? "1" : "0");
    }
    if (const auto dup = requestHeaders.find("dup"); dup != requestHeaders.end()) {
        headers.emplace("dup", dup->second);
    } else {
        headers.emplace("dup", "0");
    }
    if (const auto packetId = requestHeaders.find("packetid"); packetId != requestHeaders.end()) {
        headers.emplace("packetid", packetId->second);
    }

    httplib::Client client{endpoint.host, static_cast<int>(endpoint.port)};
    client.set_connection_timeout(0, k_legacy_listener_timeout_us);
    client.set_read_timeout(0, k_legacy_listener_timeout_us);

    const auto response = client.Put("/publish", headers, payload.stringify(), "application/json");
    return response != nullptr && (response->status == k_httpStatusOk || response->status == k_httpStatusNoContent);
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

[[nodiscard]] bool ensureSupportedRequestVersion(
    const bool errorLoggingEnabled,
    const std::string_view operationName,
    const httplib::Request& request,
    httplib::Response& response) {
    const auto versionIterator = request.headers.find("version");
    if (versionIterator == request.headers.end() || versionIterator->second == "1.0") {
        return true;
    }

    logHttpMqttError(
        errorLoggingEnabled,
        operationName,
        "unsupported version",
        std::string{"version="} + versionIterator->second + " not supported raw_body=" + request.body);
    applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "unsupported_version"), response);
    return false;
}

[[nodiscard]] std::string withRawBodyDetail(const std::string& detailText, const httplib::Request& request) {
    return detailText + " raw_body=" + request.body;
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

void applyBoolConfigWithFallback(
    const IniDocument& iniDocument,
    bool& configValue,
    const std::string_view sectionName,
    const std::string_view keyName) {
    const auto [maybeBoolValue, parseError] = iniDocument.readBool(sectionName, keyName);
    if (!parseError.empty()) {
        const std::string rawValue = iniDocument.lastValue(sectionName, keyName).value_or("<missing>");
        logConfigFallbackWarning(
            "http_mqtt_interface_client",
            sectionName,
            keyName,
            rawValue,
            configValue ? "true" : "false",
            parseError);
    }
    if (maybeBoolValue.has_value()) {
        configValue = *maybeBoolValue;
    }
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

    std::mutex legacyListenerMutex{};
    std::unordered_map<std::string, LegacyListenerEndpoint> legacyListenerBySendToken{};
    std::unordered_map<std::string, std::string> receiveTokenBySendToken{};
    bool legacyDispatchStopRequested{false};
    std::thread legacyDispatchThread{};

    bool connectedClientsReportStopRequested{false};
    std::thread connectedClientsReportThread{};
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
                      // NOLINTNEXTLINE(readability-function-cognitive-complexity)
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "publish", request, response)) {
                              return;
                          }

                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          const std::string token = resolveToken(request, fields, jsonBody);
                          const std::string clientId = resolveClientIdForRequest(impl_->sessionManager, jsonBody, token);
                          const std::string topic = resolveTopicForRequest(fields, jsonBody);
                          logIncomingPublishRequest(
                              impl_->config.logIncomingRequests,
                              request,
                              k_publishEndpoint,
                              clientId,
                              token,
                              topic);
                          try {
                              HttpMqttHeaders compatibilityFields = collectHeaders(request);
                              for (const auto& [fieldName, fieldValue] : fields) {
                                  compatibilityFields[fieldName] = fieldValue;
                              }

                              const HttpMqttPublishCompatibilityRequest compatibilityRequest{
                                  .method = request.method,
                                  .endpoint = std::string{k_publishEndpoint},
                                  .headers = collectHeaders(request),
                                  .fields = compatibilityFields,
                                  .body = request.body,
                                  .token = token,
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

                                      const bool useManagedSessionPath =
                                          !sessionToken.empty() && impl_->sessionManager.hasSession(sessionToken);
                                      const std::string_view dispatchPath = useManagedSessionPath
                                          ? std::string_view{"managed_session"}
                                          : std::string_view{"legacy_callback"};
                                      logBrokerPublishDispatchAttempt(
                                          impl_->config.logEvents,
                                          mappedMessage,
                                          sessionToken,
                                          dispatchPath);

                                      if (useManagedSessionPath) {
                                          std::string sessionError{};
                                          if (!impl_->sessionManager.publish(sessionToken, mappedMessage, sessionError)) {
                                              const std::string publishReason = sessionError.empty()
                                                  ? "broker publish callback failed"
                                                  : sessionError;
                                              logBrokerPublishDispatchFailed(
                                                  impl_->config.logErrors,
                                                  mappedMessage,
                                                  sessionToken,
                                                  dispatchPath,
                                                  publishReason);
                                              logBrokerForwardPublishError(
                                                  impl_->config.logBrokerMessages,
                                                  mappedMessage,
                                                  publishReason);
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
                                              logBrokerPublishDispatchFailed(
                                                  impl_->config.logErrors,
                                                  mappedMessage,
                                                  sessionToken,
                                                  dispatchPath,
                                                  publishReason);
                                              logBrokerForwardPublishError(
                                                  impl_->config.logBrokerMessages,
                                                  mappedMessage,
                                                  publishReason);
                                              throw YahaError{
                                                  k_error_code_broker_publish_failed,
                                                  publishReason,
                                                  "broker publish failed",
                                              };
                                          }
                                      }

                                      logBrokerPublishDispatchSent(
                                          impl_->config.logEvents,
                                          mappedMessage,
                                          sessionToken,
                                          dispatchPath);
                                      logBrokerForwardPublishAck(impl_->config.logBrokerMessages, mappedMessage);
                                      return impl_->interfaces.onPublish(downstreamRequest.headers);
                                  });

                              if (compatibilityResult.statusCode >= k_httpStatusInternalServerError) {
                                  logCompatibilityInternalResultFailure(
                                      impl_->config.logErrors,
                                      k_publishEndpoint,
                                      compatibilityResult);
                              }
                              applyHttpMqttResult(compatibilityResult, response);
                          } catch (const std::exception& exceptionValue) {
                              logCompatibilityRequestFailure(
                                  impl_->config.logErrors,
                                  k_publishEndpoint,
                                  withRawBodyDetail(exceptionValue.what(), request));
                              applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                          } catch (...) {
                              logCompatibilityRequestFailure(
                                  impl_->config.logErrors,
                                  k_publishEndpoint,
                                  withRawBodyDetail("unknown publish request error", request));
                              applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                          }
                      });

    impl_->server.Put(k_pubrelEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "pubrel", request, response)) {
                              return;
                          }

                          try {
                              applyHttpMqttResult(impl_->interfaces.onPubrel(collectHeaders(request)), response);
                          } catch (const std::exception& exceptionValue) {
                              logCompatibilityRequestFailure(
                                  impl_->config.logErrors,
                                  k_pubrelEndpoint,
                                  withRawBodyDetail(exceptionValue.what(), request));
                              applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                          } catch (...) {
                              logCompatibilityRequestFailure(
                                  impl_->config.logErrors,
                                  k_pubrelEndpoint,
                                  withRawBodyDetail("unknown publish request error", request));
                              applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                          }
                      });

    impl_->server.Put(k_connectEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "connect", request, response)) {
                              return;
                          }

                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "connect",
                                  "invalid json payload",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          const auto clientId = jsonBody.has_value()
                              ? tryReadStringField(*jsonBody, "clientId")
                              : std::nullopt;
                          if (!clientId.has_value() || clientId->empty()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "connect",
                                  "missing clientId",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_client_id"), response);
                              return;
                          }

                          HttpMqttSessionConnectRequest connectRequest{};
                          connectRequest.clientId = *clientId;
                          if (jsonBody.has_value()) {
                              // Compatibility rule:
                              // - legacy TypeScript connect payload uses host/port for client callback listener.
                              // - broker target overrides are accepted only via explicit brokerHost/brokerPort.
                              connectRequest.brokerHost = tryReadStringField(*jsonBody, "brokerHost");
                              connectRequest.brokerPort = tryReadUInt16Field(*jsonBody, "brokerPort");

                              const auto keepAliveSeconds = tryReadUInt32Field(*jsonBody, "keepAliveSeconds");
                              if (keepAliveSeconds.has_value()) {
                                  connectRequest.keepAliveSeconds = keepAliveSeconds;
                              } else {
                                  connectRequest.keepAliveSeconds = tryReadUInt32Field(*jsonBody, "keepAlive");
                              }
                          }

                          HttpMqttSessionConnectTokens tokens{};
                          std::string connectError{};
                          if (!impl_->sessionManager.connect(connectRequest, tokens, connectError)) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "connect",
                                  "session connect failed",
                                  withRawBodyDetail(connectError, request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "connect_failed"), response);
                              return;
                          }

                          if (jsonBody.has_value()) {
                              const auto legacyListenerHost = tryReadStringField(*jsonBody, "host");
                              const auto legacyListenerPort = tryReadUInt16Field(*jsonBody, "port");
                              if (legacyListenerHost.has_value() && !legacyListenerHost->empty() && legacyListenerPort.has_value()) {
                                  std::lock_guard<std::mutex> lock{impl_->legacyListenerMutex};
                                  impl_->legacyListenerBySendToken[tokens.sendToken] = LegacyListenerEndpoint{
                                      .host = *legacyListenerHost,
                                      .port = *legacyListenerPort,
                                  };
                                  impl_->receiveTokenBySendToken[tokens.sendToken] = tokens.receiveToken;
                              }
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
                              logHttpMqttEvent(
                                  impl_->config.logEvents,
                                  "connect",
                                  std::string{"clientId="} + *clientId +
                                      " sendToken=" + tokens.sendToken +
                                      " receiveToken=" + tokens.receiveToken);
                          } catch (...) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "connect",
                                  "connect response mapping failed",
                                  withRawBodyDetail("request_processing_failed", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "connect_response_failed"), response);
                          }
                      });

    impl_->server.Put(k_subscribeEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "subscribe", request, response)) {
                              return;
                          }

                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "subscribe",
                                  "invalid json payload",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          std::string topicsError{};
                          const std::map<std::string, Qos> topics = parseTopicsObject(jsonBody, topicsError, "subscribe");
                          if (!topicsError.empty()) {
                              const std::string token = resolveToken(request, fields, jsonBody);
                              const std::string clientId = resolveClientIdForRequest(impl_->sessionManager, jsonBody, token);
                              std::string detailText =
                                  "request_invalid: expected JSON field 'topics' or legacy 'subscribe' as object {\"topic/filter\": qos0..2}; parse_error=" +
                                  topicsError;
                              if (!clientId.empty()) {
                                  detailText += " clientId=" + clientId;
                              }
                              if (!token.empty()) {
                                  detailText += " token=" + token;
                              }
                              detailText += " raw_body=" + request.body;
                              logHttpMqttError(impl_->config.logErrors, "subscribe", "invalid topics payload", detailText);
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_topics"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          std::string resolvedToken = token;
                          if (resolvedToken.empty() && jsonBody.has_value()) {
                              if (const auto clientId = tryReadStringField(*jsonBody, "clientId"); clientId.has_value()) {
                                  const bool clientResolved = impl_->sessionManager.resolveSendTokenByClientId(
                                      *clientId,
                                      resolvedToken);
                                  (void)clientResolved;
                              }
                          }
                          if (resolvedToken.empty()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "subscribe",
                                  "missing token",
                                  withRawBodyDetail("request_invalid", request));
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
                          if (!impl_->sessionManager.subscribe(resolvedToken, topics, subscribeResult, subscribeError)) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "subscribe",
                                  "session subscribe failed",
                                  withRawBodyDetail(subscribeError, request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "subscribe_failed"), response);
                              return;
                          }

                          std::string resolvedClientId{};
                          (void)impl_->sessionManager.resolveClientIdByToken(resolvedToken, resolvedClientId);

                          try {
                              applyHttpMqttResult(impl_->interfaces.onSubscribe(headers, subscribeResult), response);
                              std::ostringstream detailText{};
                              if (!resolvedClientId.empty()) {
                                  detailText << "clientId=" << resolvedClientId << ' ';
                              }
                              detailText << "token=" << resolvedToken
                                         << " topics_count=" << topics.size();
                              logHttpMqttEvent(impl_->config.logEvents, "subscribe", detailText.str());
                          } catch (...) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "subscribe",
                                  "subscribe response mapping failed",
                                  withRawBodyDetail("request_processing_failed", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "subscribe_response_failed"), response);
                          }
                      });

    impl_->server.Put(k_unsubscribeEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "unsubscribe", request, response)) {
                              return;
                          }

                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "unsubscribe",
                                  "invalid json payload",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          std::string topicsError{};
                          const std::map<std::string, Qos> topics = parseTopicsObject(jsonBody, topicsError, "unsubscribe");
                          if (!topicsError.empty()) {
                              const std::string token = resolveToken(request, fields, jsonBody);
                              const std::string clientId = resolveClientIdForRequest(impl_->sessionManager, jsonBody, token);
                              std::string detailText =
                                  "request_invalid: expected JSON field 'topics' or legacy 'unsubscribe' as object {\"topic/filter\": qos0..2}; parse_error=" +
                                  topicsError;
                              if (!clientId.empty()) {
                                  detailText += " clientId=" + clientId;
                              }
                              if (!token.empty()) {
                                  detailText += " token=" + token;
                              }
                              detailText += " raw_body=" + request.body;
                              logHttpMqttError(impl_->config.logErrors, "unsubscribe", "invalid topics payload", detailText);
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_topics"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          std::string resolvedToken = token;
                          if (resolvedToken.empty() && jsonBody.has_value()) {
                              if (const auto clientId = tryReadStringField(*jsonBody, "clientId"); clientId.has_value()) {
                                  const bool clientResolved = impl_->sessionManager.resolveSendTokenByClientId(
                                      *clientId,
                                      resolvedToken);
                                  (void)clientResolved;
                              }
                          }
                          if (resolvedToken.empty()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "unsubscribe",
                                  "missing token",
                                  withRawBodyDetail("request_invalid", request));
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
                          if (!impl_->sessionManager.unsubscribe(resolvedToken, topics, unsubscribeResult, unsubscribeError)) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "unsubscribe",
                                  "session unsubscribe failed",
                                  withRawBodyDetail(unsubscribeError, request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "unsubscribe_failed"), response);
                              return;
                          }

                          std::string resolvedClientId{};
                          (void)impl_->sessionManager.resolveClientIdByToken(resolvedToken, resolvedClientId);

                          try {
                              applyHttpMqttResult(impl_->interfaces.onUnsubscribe(headers, unsubscribeResult), response);
                              std::ostringstream detailText{};
                              if (!resolvedClientId.empty()) {
                                  detailText << "clientId=" << resolvedClientId << ' ';
                              }
                              detailText << "token=" << resolvedToken
                                         << " topics_count=" << topics.size();
                              logHttpMqttEvent(impl_->config.logEvents, "unsubscribe", detailText.str());
                          } catch (...) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "unsubscribe",
                                  "unsubscribe response mapping failed",
                                  withRawBodyDetail("request_processing_failed", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "unsubscribe_response_failed"), response);
                          }
                      });

    impl_->server.Put(k_disconnectEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "disconnect", request, response)) {
                              return;
                          }

                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "disconnect",
                                  "invalid json payload",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          std::string resolvedToken = token;
                          if (resolvedToken.empty() && jsonBody.has_value()) {
                              if (const auto clientId = tryReadStringField(*jsonBody, "clientId"); clientId.has_value()) {
                                  const bool clientResolved = impl_->sessionManager.resolveSendTokenByClientId(
                                      *clientId,
                                      resolvedToken);
                                  (void)clientResolved;
                              }
                          }
                          if (resolvedToken.empty()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "disconnect",
                                  "missing token",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_token"), response);
                              return;
                          }

                          std::string disconnectError{};
                          std::string disconnectClientId{};
                          (void)impl_->sessionManager.resolveClientIdByToken(resolvedToken, disconnectClientId);
                          if (!impl_->sessionManager.disconnect(resolvedToken, disconnectError)) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "disconnect",
                                  "session disconnect failed",
                                  withRawBodyDetail(disconnectError, request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "disconnect_failed"), response);
                              return;
                          }

                          {
                              std::lock_guard<std::mutex> lock{impl_->legacyListenerMutex};
                              impl_->legacyListenerBySendToken.erase(resolvedToken);
                              impl_->receiveTokenBySendToken.erase(resolvedToken);
                          }

                          HttpMqttHeaders headers = collectHeaders(request);
                          if (headers.find("version") == headers.end()) {
                              headers["version"] = "1.0";
                          }

                          try {
                              applyHttpMqttResult(impl_->interfaces.onDisconnect(headers), response);
                              std::string detailText{"token=" + resolvedToken};
                              if (!disconnectClientId.empty()) {
                                  detailText += " clientId=" + disconnectClientId;
                              }
                              logHttpMqttEvent(impl_->config.logEvents, "disconnect", detailText);
                          } catch (...) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "disconnect",
                                  "disconnect response mapping failed",
                                  withRawBodyDetail("request_processing_failed", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "disconnect_response_failed"), response);
                          }
                      });

    impl_->server.Put(k_pingEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "ping", request, response)) {
                              return;
                          }

                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "ping",
                                  "invalid json payload",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          if (token.empty()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "ping",
                                  "missing token",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_token"), response);
                              return;
                          }

                          std::string resolvedClientId{};
                          (void)impl_->sessionManager.resolveClientIdByToken(token, resolvedClientId);

                          std::string pingError{};
                          if (!impl_->sessionManager.ping(token, pingError)) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "ping",
                                  "session ping failed",
                                  withRawBodyDetail(pingError, request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "ping_failed"), response);
                              return;
                          }

                          applyHttpMqttResult(makeNoContentResult("pingresp"), response);
                          std::ostringstream detailText{};
                          if (!resolvedClientId.empty()) {
                              detailText << "clientId=" << resolvedClientId << ' ';
                          }
                          detailText << "token=" << token;
                          logHttpMqttEvent(impl_->config.logEvents, "ping", detailText.str());
                      });

    impl_->server.Put(k_receiveEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "receive", request, response)) {
                              return;
                          }

                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "receive",
                                  "invalid json payload",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          if (token.empty()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "receive",
                                  "missing token",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_token"), response);
                              return;
                          }

                          std::optional<Message> receivedMessage{};
                          std::string receiveError{};
                          if (!impl_->sessionManager.receive(token, receivedMessage, receiveError)) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "receive",
                                  "session receive failed",
                                  withRawBodyDetail(receiveError, request));
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
                          logBrokerIncomingMessage(impl_->config.logBrokerMessages, *receivedMessage);
                      });

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    const auto handleCompatibilityRequest = [this](
                                                const httplib::Request& request,
                                                httplib::Response& response,
                                                const std::string_view endpoint) {
        try {
            if (!ensureSupportedRequestVersion(impl_->config.logErrors, "publish_compatibility", request, response)) {
                return;
            }

            const HttpMqttHeaders headers = collectHeaders(request);
            const HttpMqttHeaders fields = collectFields(request);
            const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
            const std::string token = resolveToken(request, fields, jsonBody);
            const std::string clientId = resolveClientIdForRequest(impl_->sessionManager, jsonBody, token);
            const std::string topic = resolveTopicForRequest(fields, jsonBody);
            logIncomingPublishRequest(
                impl_->config.logIncomingRequests,
                request,
                endpoint,
                clientId,
                token,
                topic);
            HttpMqttHeaders compatibilityFields = headers;
            for (const auto& [fieldName, fieldValue] : fields) {
                compatibilityFields[fieldName] = fieldValue;
            }
            const HttpMqttPublishCompatibilityRequest compatibilityRequest{
                .method = request.method,
                .endpoint = std::string{endpoint},
                .headers = headers,
                .fields = compatibilityFields,
                .body = request.body,
                .token = token,
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

                    const bool useManagedSessionPath =
                        !sessionToken.empty() && impl_->sessionManager.hasSession(sessionToken);
                    const std::string_view dispatchPath = useManagedSessionPath
                        ? std::string_view{"managed_session"}
                        : std::string_view{"legacy_callback"};
                    logBrokerPublishDispatchAttempt(
                        impl_->config.logEvents,
                        mappedMessage,
                        sessionToken,
                        dispatchPath);

                    if (useManagedSessionPath) {
                        std::string sessionError{};
                        if (!impl_->sessionManager.publish(sessionToken, mappedMessage, sessionError)) {
                            const std::string publishReason = sessionError.empty()
                                ? "broker publish callback failed"
                                : sessionError;
                            logBrokerPublishDispatchFailed(
                                impl_->config.logErrors,
                                mappedMessage,
                                sessionToken,
                                dispatchPath,
                                publishReason);
                            logBrokerForwardPublishError(impl_->config.logBrokerMessages, mappedMessage, publishReason);
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
                            logBrokerPublishDispatchFailed(
                                impl_->config.logErrors,
                                mappedMessage,
                                sessionToken,
                                dispatchPath,
                                publishReason);
                            logBrokerForwardPublishError(impl_->config.logBrokerMessages, mappedMessage, publishReason);
                            throw YahaError{
                                k_error_code_broker_publish_failed,
                                publishReason,
                                "broker publish failed",
                            };
                        }
                    }

                    logBrokerPublishDispatchSent(
                        impl_->config.logEvents,
                        mappedMessage,
                        sessionToken,
                        dispatchPath);
                    logBrokerForwardPublishAck(impl_->config.logBrokerMessages, mappedMessage);
                    return impl_->interfaces.onPublish(downstreamRequest.headers);
                });

            if (compatibilityResult.statusCode >= k_httpStatusInternalServerError) {
                logCompatibilityInternalResultFailure(impl_->config.logErrors, endpoint, compatibilityResult);
            }
            applyHttpMqttResult(compatibilityResult, response);
        } catch (const std::exception& exceptionValue) {
            logCompatibilityRequestFailure(
                impl_->config.logErrors,
                endpoint,
                withRawBodyDetail(exceptionValue.what(), request));
            applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
        } catch (...) {
            logCompatibilityRequestFailure(
                impl_->config.logErrors,
                endpoint,
                withRawBodyDetail("unknown publish request error", request));
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

                logBrokerIncomingMessage(impl_->config.logBrokerMessages, *receivedMessage);

                const bool forwarded = forwardLegacyListenerPublish(
                    endpoint,
                    *receivedMessage,
                    HttpMqttHeaders{});
                std::string resolvedClientId{};
                (void)impl_->sessionManager.resolveClientIdByToken(sendToken, resolvedClientId);
                logLegacyForwardResult(
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

    const auto [maybeConnectedClientsReportIntervalSeconds, connectedClientsReportIntervalError] =
        iniDocument.readUnsigned(
            k_httpSection,
            k_connectedClientsReportIntervalSecondsKey,
            1U,
            86400U);
    if (!connectedClientsReportIntervalError.empty()) {
        const std::string rawValue =
            iniDocument.lastValue(k_httpSection, k_connectedClientsReportIntervalSecondsKey).value_or("<missing>");
        logConfigFallbackWarning(
            "http_mqtt_interface_client",
            k_httpSection,
            k_connectedClientsReportIntervalSecondsKey,
            rawValue,
            std::to_string(configOutput.connectedClientsReportIntervalSeconds),
            connectedClientsReportIntervalError);
    }
    if (maybeConnectedClientsReportIntervalSeconds.has_value()) {
        configOutput.connectedClientsReportIntervalSeconds = *maybeConnectedClientsReportIntervalSeconds;
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
