#include "yaha/http_mqtt_interface/internal/http_mqtt_interface_operations_internal.h"

#include "yaha/http_mqtt_interface/http_mqtt_interface_contracts.h"
#include "yaha/http_mqtt_interface/http_mqtt_interface_operations.h"

#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace yaha {

using namespace http_mqtt_ops_internal;

namespace {

[[nodiscard]] HttpMqttResult makeCompatibilityErrorResponse(
    const int statusCode,
    const std::string_view errorCode) {
    HttpMqttResult responseOutput{};
    responseOutput.statusCode = statusCode;
    responseOutput.headers = makeStandardJsonHeaders();
    responseOutput.headers["version"] = std::string{k_versionValue};
    responseOutput.payload = std::format(R"({{"error":"{}"}})", errorCode);
    return responseOutput;
}

[[nodiscard]] bool isSupportedCompatibilityRoute(
    const std::string_view methodInput,
    const std::string_view endpointInput,
    const HttpMqttPublishCompatibilityConfig& configInput) {
    const std::string upperMethod = toUpperCopy(methodInput);
    if (upperMethod == "PUT" && endpointInput == "/publish") {
        return true;
    }

    if (upperMethod == "POST" && endpointInput == "/publish") {
        return true;
    }

    if (upperMethod == "POST" && endpointInput == "/publish.php") {
        return configInput.enablePublishPhpAlias;
    }

    return false;
}

struct CompatibilityBodyFields {
    std::optional<std::string> topic{};
    std::optional<Value> value{};
    std::optional<ReasonList> reason{};
    std::optional<Qos> qos{};
    std::optional<bool> retain{};
};

[[nodiscard]] std::optional<CompatibilityBodyFields> tryParseCompatibilityBody(
    const std::string_view bodyText) {
    try {
        requireJsonObjectPayload(bodyText, "publish compatibility body");
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }

    CompatibilityBodyFields fieldsOutput{};

    fieldsOutput.topic = tryExtractStringField(bodyText, "topic");

    const std::optional<std::string> valueToken = extractRawToken(bodyText, "value");
    if (valueToken.has_value()) {
        const std::optional<Value> parsedValue = parseJsonValueToken(*valueToken);
        if (!parsedValue.has_value()) {
            return std::nullopt;
        }
        fieldsOutput.value = parsedValue;
    }

    const std::optional<std::string> reasonToken = extractRawToken(bodyText, "reason");
    if (reasonToken.has_value()) {
        const std::optional<ReasonList> parsedReason = parseReasonArray(*reasonToken);
        if (!parsedReason.has_value()) {
            return std::nullopt;
        }
        fieldsOutput.reason = parsedReason;
    }

    const std::optional<std::string> qosToken = extractRawToken(bodyText, "qos");
    if (qosToken.has_value()) {
        const std::optional<Qos> parsedQos = parseQosField(*qosToken);
        if (!parsedQos.has_value()) {
            return std::nullopt;
        }
        fieldsOutput.qos = parsedQos;
    }

    const std::optional<std::string> retainToken = extractRawToken(bodyText, "retain");
    if (retainToken.has_value()) {
        const std::optional<bool> parsedRetain = parseRetainField(*retainToken);
        if (!parsedRetain.has_value()) {
            return std::nullopt;
        }
        fieldsOutput.retain = parsedRetain;
    }

    return fieldsOutput;
}

struct CompatibilityParsedFields {
    std::string topic{};
    std::optional<Value> value{};
    std::optional<ReasonList> reason{};
    std::optional<Qos> qos{};
    std::optional<bool> retain{};
};

[[nodiscard]] std::optional<CompatibilityParsedFields> tryParseCompatibilityFields(
    const HttpMqttHeaders& normalizedFields) {
    CompatibilityParsedFields fieldsOutput{};

    fieldsOutput.topic = trimCopy(tryReadHeaderValue(normalizedFields, "topic").value_or(""));

    if (const auto extractedValue = tryReadHeaderValue(normalizedFields, "value"); extractedValue.has_value()) {
        fieldsOutput.value = Value{*extractedValue};
    }

    if (const auto reasonField = tryReadHeaderValue(normalizedFields, "reason"); reasonField.has_value()) {
        const std::optional<ReasonList> parsedReason = parseReasonArray(*reasonField);
        if (!parsedReason.has_value()) {
            return std::nullopt;
        }
        fieldsOutput.reason = parsedReason;
    }

    if (const auto qosField = tryReadHeaderValue(normalizedFields, "qos"); qosField.has_value()) {
        fieldsOutput.qos = parseQosField(*qosField);
    }

    if (const auto retainField = tryReadHeaderValue(normalizedFields, "retain"); retainField.has_value()) {
        fieldsOutput.retain = parseRetainField(*retainField);
    }

    return fieldsOutput;
}

[[nodiscard]] bool applyCompatibilityBodyFallback(
    CompatibilityParsedFields& parsedFields,
    const std::string_view bodyText) {
    if (!parsedFields.topic.empty()) {
        return true;
    }

    const std::string trimmedBody = trimCopy(bodyText);
    if (trimmedBody.empty()) {
        return true;
    }

    const std::optional<CompatibilityBodyFields> bodyFields = tryParseCompatibilityBody(trimmedBody);
    if (!bodyFields.has_value()) {
        return false;
    }

    if (bodyFields->topic.has_value()) {
        parsedFields.topic = trimCopy(*bodyFields->topic);
    }
    if (!parsedFields.value.has_value() && bodyFields->value.has_value()) {
        parsedFields.value = bodyFields->value;
    }
    if (!parsedFields.reason.has_value() && bodyFields->reason.has_value()) {
        parsedFields.reason = bodyFields->reason;
    }
    if (!parsedFields.qos.has_value() && bodyFields->qos.has_value()) {
        parsedFields.qos = bodyFields->qos;
    }
    if (!parsedFields.retain.has_value() && bodyFields->retain.has_value()) {
        parsedFields.retain = bodyFields->retain;
    }

    return true;
}

[[nodiscard]] std::optional<HttpMqttResult> validateCompatibilityTopic(CompatibilityParsedFields& parsedFields) {
    parsedFields.topic = decodeTopicSlashEscapes(parsedFields.topic);
    if (parsedFields.topic.empty()) {
        return makeCompatibilityErrorResponse(k_httpStatusBadRequest, "missing_topic");
    }
    return std::nullopt;
}

[[nodiscard]] Message buildCompatibilityMessage(const CompatibilityParsedFields& parsedFields) {
    Message mappedMessage{
        parsedFields.topic,
        parsedFields.value.value_or(Value{std::string{}}),
        parsedFields.qos.value_or(Qos::AtLeastOnce),
        parsedFields.retain.value_or(false)};

    if (parsedFields.reason.has_value() && !parsedFields.reason->empty()) {
        appendReasonsPreservingOrder(mappedMessage, *parsedFields.reason);
    }

    mappedMessage.addReason(std::string{k_publishIngressReasonMessage});

    return mappedMessage;
}

[[nodiscard]] HttpMqttResult tryForwardCompatibilityPublish(
    const HttpMqttPublishCompatibilityForwarder& forwarder,
    const HttpMqttRequestData& mappedRequest,
    const Message& mappedMessage) {
    HttpMqttResult downstreamResult = forwarder(mappedRequest, mappedMessage);

    if (downstreamResult.statusCode != k_httpStatusNoContent &&
        downstreamResult.statusCode != k_httpStatusOk) {
        return makeCompatibilityErrorResponse(k_httpStatusInternalServerError, "internal_failure");
    }
    return downstreamResult;
}

[[nodiscard]] HttpMqttResult adaptCompatibilityResponse(
    const HttpMqttResult& downstreamResult,
    const HttpMqttPublishCompatibilityConfig& configInput) {
    if (configInput.responseMode == HttpMqttPublishCompatibilityResponseMode::Native) {
        return downstreamResult;
    }

    HttpMqttResult compatibilityResult{};
    compatibilityResult.statusCode = k_httpStatusOk;
    compatibilityResult.headers = makeStandardJsonHeaders();
    compatibilityResult.headers["version"] = std::string{k_versionValue};
    compatibilityResult.payload = std::format("\"{}\"", escapeJsonString(downstreamResult.payload));
    return compatibilityResult;
}

} // namespace

HttpMqttResult handlePublishCompatibilityRequest(
    const HttpMqttInterfaces& interfaces,
    const HttpMqttPublishCompatibilityRequest& requestInput,
    const HttpMqttPublishCompatibilityConfig& configInput,
    const HttpMqttPublishCompatibilityForwarder& forwarder) {
    if (!isSupportedCompatibilityRoute(requestInput.method, requestInput.endpoint, configInput)) {
        return makeCompatibilityErrorResponse(k_httpStatusMethodNotAllowed, "unsupported_method");
    }

    const HttpMqttHeaders normalizedFields = normalizeHeaderKeys(requestInput.fields);
    const std::optional<CompatibilityParsedFields> parsedFields = tryParseCompatibilityFields(normalizedFields);
    if (!parsedFields.has_value()) {
        return makeCompatibilityErrorResponse(k_httpStatusBadRequest, "invalid_json");
    }

    CompatibilityParsedFields resolvedFields = *parsedFields;
    if (!applyCompatibilityBodyFallback(resolvedFields, requestInput.body)) {
        return makeCompatibilityErrorResponse(k_httpStatusBadRequest, "invalid_json");
    }

    if (const std::optional<HttpMqttResult> topicError = validateCompatibilityTopic(resolvedFields);
        topicError.has_value()) {
        return *topicError;
    }

    Message mappedMessage = buildCompatibilityMessage(resolvedFields);

    const HttpMqttPublishOptions mappedOptions{
        .token = requestInput.token,
        .message = mappedMessage,
        .dup = std::nullopt,
        .packetId = readPacketIdHeader(requestInput.headers)};
    const HttpMqttRequestData mappedRequest = interfaces.publish(k_versionValue, mappedOptions);
    mappedMessage.setRawPayload(mappedRequest.payload);

    const HttpMqttResult downstreamResult =
        tryForwardCompatibilityPublish(forwarder, mappedRequest, mappedMessage);

    return adaptCompatibilityResponse(downstreamResult, configInput);
}

} // namespace yaha
