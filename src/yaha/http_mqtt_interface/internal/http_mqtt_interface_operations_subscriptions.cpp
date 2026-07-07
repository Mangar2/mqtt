#include "yaha/http_mqtt_interface/internal/http_mqtt_interface_operations_internal.h"

#include "yaha/http_mqtt_interface/http_mqtt_interface_contracts.h"
#include "json/json_value.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace yaha {

using namespace http_mqtt_ops_internal;

HttpMqttRequestData buildSubscribeV1Request(
    const HttpMqttTopics& topicsInput,
    const std::string& clientId,
    const std::uint16_t packetId) {
    HttpMqttRequestData requestData{};
    requestData.headers = makeStandardJsonHeaders();
    requestData.headers["version"] = std::string{k_versionValue};
    requestData.headers["packetid"] = std::to_string(packetId);

    mqtt::json::JsonValue::Object payloadObject{};
    payloadObject.emplace("clientId", mqtt::json::JsonValue{clientId});

    const auto topicsObject = mqtt::json::JsonValue::try_parse(serializeTopics(topicsInput));
    if (!topicsObject.has_value() || !topicsObject->is_object()) {
        throw std::runtime_error{"subscribe request: failed to serialize topics"};
    }
    payloadObject.emplace("topics", *topicsObject);
    requestData.payload = mqtt::json::JsonValue{std::move(payloadObject)}.stringify();

    requestData.resultCheck = [expectedPacketId = packetId](const HttpMqttResult& resultInput) {
        validateStatusCode(resultInput, k_httpStatusOk, "subscribe result");
        validateContentTypeJson(resultInput, "subscribe result");
        validateHeaderEquals(resultInput, "packet", "suback", "subscribe result");
        validatePacketIdMatch(resultInput, expectedPacketId, "subscribe result");

        const std::vector<int> qosValues = parseIntegerArrayPayload(resultInput.payload);
        for (const int qosValue : qosValues) {
            if (qosValue != 0 && qosValue != 1 && qosValue != 2 &&
                qosValue != k_subscribeCodeFailLegacy && qosValue != k_subscribeCodeFailModern) {
                throw std::runtime_error{"subscribe result: unsupported qos return code"};
            }
        }
    };

    return requestData;
}

HttpMqttResult buildSubscribeV1Response(
    const HttpMqttHeaders& headersInput,
    const HttpMqttSubscribeResult& resultInput) {
    HttpMqttResult resultOutput{};
    resultOutput.statusCode = k_httpStatusOk;
    resultOutput.headers = makeStandardJsonHeaders();
    resultOutput.headers["version"] = std::string{k_versionValue};
    resultOutput.headers["packet"] = "suback";

    const auto packetId = readPacketIdHeader(headersInput);
    if (packetId.has_value()) {
        resultOutput.packetId = packetId;
        resultOutput.headers["packetid"] = std::to_string(*packetId);
    }

    const auto qosArray = mqtt::json::JsonValue::try_parse(serializeUInt8Array(resultInput));
    if (!qosArray.has_value() || !qosArray->is_array()) {
        throw std::runtime_error{"subscribe response: failed to serialize qos values"};
    }

    mqtt::json::JsonValue::Object payloadObject{};
    payloadObject.emplace("qos", *qosArray);
    resultOutput.payload = mqtt::json::JsonValue{std::move(payloadObject)}.stringify();
    return resultOutput;
}

HttpMqttRequestData buildUnsubscribeV1Request(
    const HttpMqttTopics& topicsInput,
    const std::string& clientId,
    const std::uint16_t packetId) {
    HttpMqttRequestData requestData{};
    requestData.headers = makeStandardJsonHeaders();
    requestData.headers["version"] = std::string{k_versionValue};
    requestData.headers["packetid"] = std::to_string(packetId);

    mqtt::json::JsonValue::Object payloadObject{};
    payloadObject.emplace("clientId", mqtt::json::JsonValue{clientId});

    const auto topicsObject = mqtt::json::JsonValue::try_parse(serializeTopics(topicsInput));
    if (!topicsObject.has_value() || !topicsObject->is_object()) {
        throw std::runtime_error{"unsubscribe request: failed to serialize topics"};
    }
    payloadObject.emplace("topics", *topicsObject);
    requestData.payload = mqtt::json::JsonValue{std::move(payloadObject)}.stringify();

    requestData.resultCheck = [expectedPacketId = packetId](const HttpMqttResult& resultInput) {
        if (resultInput.statusCode != k_httpStatusOk &&
            resultInput.statusCode != k_httpStatusNoContent) {
            throw std::runtime_error{"unsubscribe result: invalid status"};
        }

        validateContentTypeJson(resultInput, "unsubscribe result");
        validateHeaderEquals(resultInput, "packet", "unsuback", "unsubscribe result");
        validatePacketIdMatch(resultInput, expectedPacketId, "unsubscribe result");

        if (resultInput.statusCode == k_httpStatusNoContent && resultInput.payload.empty()) {
            return;
        }

        const std::vector<int> returnCodes = parseIntegerArrayPayload(resultInput.payload);
        for (const int returnCode : returnCodes) {
            if (returnCode != 0 && returnCode != k_unsubscribeNoSubscription) {
                throw std::runtime_error{"unsubscribe result: unsupported return code"};
            }
        }
    };

    return requestData;
}

HttpMqttResult buildUnsubscribeV1Response(
    const HttpMqttHeaders& headersInput,
    const HttpMqttUnsubscribeResult& resultInput) {
    HttpMqttResult resultOutput{};
    resultOutput.statusCode = k_httpStatusOk;
    resultOutput.headers = makeStandardJsonHeaders();
    resultOutput.headers["version"] = std::string{k_versionValue};
    resultOutput.headers["packet"] = "unsuback";

    const auto packetId = readPacketIdHeader(headersInput);
    if (packetId.has_value()) {
        resultOutput.packetId = packetId;
        resultOutput.headers["packetid"] = std::to_string(*packetId);
    }

    resultOutput.payload = serializeUInt8Array(resultInput);
    return resultOutput;
}

} // namespace yaha
