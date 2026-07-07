#include "yaha/http_mqtt_interface_client/internal/http_mqtt_interface_client_app_internal.h"

#include "yaha/message/message_log_service.h"
#include "yaha/message/message_payload_codec.h"
#include "json/json_value.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yaha::http_mqtt_interface_client_internal {

void logHttpMqttEvent(const bool enabled, const std::string_view eventName, const std::string_view detailText) {
    if (!enabled) {
        return;
    }

    std::cout << "http_mqtt_interface_client[event] " << eventName;
    if (!detailText.empty()) {
        std::cout << ' ' << detailText;
    }
    std::cout << '\n' << std::flush;
}

void logHttpMqttTrace(const bool enabled, const std::string_view traceName, const std::string_view detailText) {
    if (!enabled) {
        return;
    }

    std::cout << "http_mqtt_interface_client[trace] " << traceName;
    if (!detailText.empty()) {
        std::cout << ' ' << detailText;
    }
    std::cout << '\n' << std::flush;
}

void logHttpMqttError(
    const bool enabled,
    const std::string_view operationName,
    const std::string_view reasonText,
    const std::string_view detailText) {
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

[[nodiscard]] std::optional<std::string> buildBrokerForwardLogLine(const Message& message, const bool includeReasonChain) {
    const MessageLogConfig logConfig{
        .enableIncoming = false,
        .enableOutgoing = true,
        .includeReasonChain = includeReasonChain,
    };

    return buildMessageLogLine(
        "http_mqtt_interface_client",
        MessageLogDirection::Outgoing,
        message,
        logConfig);
}

[[nodiscard]] bool isBrokerNoAckError(const std::string_view errorText) {
    return errorText.find("timed out waiting for PUBACK") != std::string_view::npos ||
           errorText.find("timed out waiting for PUBREC") != std::string_view::npos ||
           errorText.find("timed out waiting for PUBCOMP") != std::string_view::npos;
}

void logBrokerForwardPublishAck(const bool enabled, const bool includeReasonChain, const Message& message) {
    if (!enabled) {
        return;
    }

    if (const auto line = buildBrokerForwardLogLine(message, includeReasonChain); line.has_value()) {
        std::cout << *line << " event=broker_publish_ack" << '\n' << std::flush;
    }
}

void logBrokerIncomingMessage(const bool enabled, const bool includeReasonChain, const Message& message) {
    if (!enabled) {
        return;
    }

    const MessageLogConfig logConfig{
        .enableIncoming = true,
        .enableOutgoing = false,
        .includeReasonChain = includeReasonChain,
    };

    if (const auto line = buildMessageLogLine(
            "http_mqtt_interface_client",
            MessageLogDirection::Incoming,
            message,
            logConfig);
        line.has_value()) {
        std::cout << *line << " event=broker_message_in" << '\n' << std::flush;
    }
}

void logListenerForwardResult(
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
        logHttpMqttEvent(eventLoggingEnabled, "listener_forward", details.str());
    } else {
        logHttpMqttError(errorLoggingEnabled, "listener_forward", "forward failed", details.str());
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
    const bool includeReasonChain,
    const Message& message,
    const std::string_view errorText) {
    if (!enabled) {
        return;
    }

    if (const auto line = buildBrokerForwardLogLine(message, includeReasonChain); line.has_value()) {
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
    logHttpMqttTrace(enabled, "broker_publish_dispatch", detail.str());
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
    logHttpMqttTrace(enabled, "broker_publish_sent", detail.str());
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

[[nodiscard]] std::optional<int> tryParseTopicQos(
    const mqtt::json::JsonValue& qosValue,
    std::string& errorText) {
    int qosNumber = -1;
    if (qosValue.is_number()) {
        qosNumber = static_cast<int>(qosValue.as_number());
    } else if (qosValue.is_string()) {
        const std::string& qosText = qosValue.as_string();
        if (qosText == "0") {
            qosNumber = 0;
        } else if (qosText == "1") {
            qosNumber = 1;
        } else if (qosText == "2") {
            qosNumber = 2;
        } else {
            errorText = "topic qos string must be one of 0,1,2";
            return std::nullopt;
        }
    } else {
        errorText = "topic qos must be numeric or numeric-string";
        return std::nullopt;
    }

    if (qosNumber < 0 || qosNumber > 2) {
        errorText = "topic qos out of range";
        return std::nullopt;
    }

    return qosNumber;
}

[[nodiscard]] bool parseSharedQosTopicsShape(
    const mqtt::json::JsonValue& topicsObject,
    std::map<std::string, Qos>& topics,
    std::string& errorText) {
    if (!topicsObject.contains("QoS") || !topicsObject.contains("topics")) {
        return false;
    }

    const auto qosNumber = tryParseTopicQos(topicsObject.at("QoS"), errorText);
    if (!qosNumber.has_value()) {
        topics.clear();
        return true;
    }

    const auto& sharedTopics = topicsObject.at("topics");
    if (sharedTopics.is_string()) {
        topics[sharedTopics.as_string()] = static_cast<Qos>(*qosNumber);
        return true;
    }
    if (!sharedTopics.is_array()) {
        errorText = "topics must be string or array of strings";
        topics.clear();
        return true;
    }

    for (const auto& topicValue : sharedTopics.as_array()) {
        if (!topicValue.is_string()) {
            errorText = "topics array must contain only strings";
            topics.clear();
            return true;
        }
        topics[topicValue.as_string()] = static_cast<Qos>(*qosNumber);
    }
    return true;
}

[[nodiscard]] std::map<std::string, Qos> parseTopicsObject(
    const std::optional<mqtt::json::JsonValue>& jsonBody,
    std::string& errorText,
    const std::string_view legacyFieldName) {
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

    if (parseSharedQosTopicsShape(topicsObject, topics, errorText)) {
        return topics;
    }

    for (const auto& [topic, qosValue] : topicsObject.as_object()) {
        const auto qosNumber = tryParseTopicQos(qosValue, errorText);
        if (!qosNumber.has_value()) {
            topics.clear();
            return topics;
        }
        topics[topic] = static_cast<Qos>(*qosNumber);
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

[[nodiscard]] HttpMqttResult makeNoContentResult(const std::string_view packetName) {
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

void applyBoolConfigWithFallback(
    const IniDocument& iniDocument,
    bool& configValue,
    const std::string_view sectionName,
    const std::string_view keyName) {
    const auto maybeBoolValue = iniDocument.readBool(sectionName, keyName, configValue);
    if (maybeBoolValue.has_value()) {
        configValue = *maybeBoolValue;
    }
}

} // namespace yaha::http_mqtt_interface_client_internal

