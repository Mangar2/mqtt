#include "yaha/http_mqtt_interface/internal/http_mqtt_interface_operations_internal.h"

#include "yaha/http_mqtt_interface/http_mqtt_interface_contracts.h"

#include <charconv>
#include <format>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace yaha {

using namespace http_mqtt_ops_internal;

namespace {

[[nodiscard]] std::string mqttCodeErrorMessage(const int mqttCode) {
    switch (mqttCode) {
        case 1:
            return "mqtt connect error: unacceptable protocol version";
        case 2:
            return "mqtt connect error: identifier rejected";
        case 3:
            return "mqtt connect error: server unavailable";
        case 4:
            return "mqtt connect error: bad username or password";
        case k_connectCodeMax:
            return "mqtt connect error: not authorized";
        default:
            return std::format("mqtt connect error: code {}", mqttCode);
    }
}

} // namespace

HttpMqttRequestData buildConnectV1Request(const HttpMqttConnectOptions& optionsInput) {
    HttpMqttRequestData requestData{};
    requestData.headers = makeStandardJsonHeaders();
    requestData.headers["version"] = std::string{k_versionValue};

    const std::uint32_t keepAliveValue = optionsInput.keepAlive.value_or(0U);
    std::ostringstream outputStream{};
    outputStream << '{';
    outputStream << std::format("\"clean\":{}", optionsInput.clean ? "true" : "false");
    outputStream << std::format(",\"keepAlive\":{}", static_cast<unsigned long long>(keepAliveValue));
    if (optionsInput.clientId.has_value()) {
        outputStream << std::format(R"(,"clientId":"{}")", escapeJsonString(*optionsInput.clientId));
    }
    if (optionsInput.host.has_value()) {
        outputStream << std::format(R"(,"host":"{}")", escapeJsonString(*optionsInput.host));
    }
    if (optionsInput.port.has_value()) {
        outputStream << std::format(",\"port\":{}", static_cast<unsigned int>(*optionsInput.port));
    }
    if (optionsInput.user.has_value()) {
        outputStream << std::format(R"(,"user":"{}")", escapeJsonString(*optionsInput.user));
    }
    if (optionsInput.password.has_value()) {
        outputStream << std::format(R"(,"password":"{}")", escapeJsonString(*optionsInput.password));
    }
    outputStream << '}';
    requestData.payload = outputStream.str();

    requestData.resultCheck = [](const HttpMqttResult& resultInput) {
        validateStatusCode(resultInput, k_httpStatusOk, "connect result");
        validateContentTypeJson(resultInput, "connect result");
        validateHeaderEquals(resultInput, "packet", "connack", "connect result");
        requireJsonObjectPayload(resultInput.payload, "connect result");

        const std::optional<int> presentValue = tryExtractIntegerField(resultInput.payload, "present");
        if (!presentValue.has_value() || (*presentValue != 0 && *presentValue != 1)) {
            throw std::runtime_error{"connect result: present must be 0 or 1"};
        }

        const std::optional<int> mqttCode = tryExtractIntegerField(resultInput.payload, "mqttcode");
        if (mqttCode.has_value()) {
            if (*mqttCode == 0) {
                return;
            }
            if (*mqttCode >= 1 && *mqttCode <= k_connectCodeMax) {
                throw std::runtime_error{mqttCodeErrorMessage(*mqttCode)};
            }
            throw std::runtime_error{"connect result: invalid mqttcode"};
        }

        const std::optional<std::string> tokenObject = extractRawToken(resultInput.payload, "token");
        if (!tokenObject.has_value()) {
            throw std::runtime_error{"connect result: missing token object"};
        }

        const std::optional<std::string> sendToken = tryExtractStringField(*tokenObject, "send");
        const std::optional<std::string> receiveToken = tryExtractStringField(*tokenObject, "receive");
        if (!sendToken.has_value() || !receiveToken.has_value() ||
            sendToken->empty() || receiveToken->empty()) {
            throw std::runtime_error{"connect result: token.send and token.receive must be strings"};
        }
    };

    return requestData;
}

HttpMqttResult buildConnectV1Response(const HttpMqttConnectResult& resultInput) {
    if (resultInput.present != 0U && resultInput.present != 1U) {
        throw std::runtime_error{"onConnect: present must be 0 or 1"};
    }

    HttpMqttResult resultOutput{};
    resultOutput.statusCode = k_httpStatusOk;
    resultOutput.headers = makeStandardJsonHeaders();
    resultOutput.headers["packet"] = "connack";
    resultOutput.headers["version"] = std::string{k_versionValue};

    std::ostringstream outputStream{};
    outputStream << '{';
    if (resultInput.mqttCode.has_value()) {
        outputStream << std::format(
            "\"mqttcode\":{},",
            static_cast<unsigned int>(*resultInput.mqttCode));
    }
    outputStream << std::format("\"present\":{}", static_cast<unsigned int>(resultInput.present));
    outputStream << std::format(
        R"(,"token":{{"send":"{}","receive":"{}"}})",
        escapeJsonString(resultInput.token.send),
        escapeJsonString(resultInput.token.receive));
    outputStream << '}';

    resultOutput.payload = outputStream.str();
    return resultOutput;
}

HttpMqttRequestData buildDisconnectV1Request(const std::string& clientId) {
    HttpMqttRequestData requestData{};
    requestData.headers = makeStandardJsonHeaders();
    requestData.headers["version"] = std::string{k_versionValue};
    requestData.payload = std::format(R"({{"clientId":"{}"}})", escapeJsonString(clientId));
    requestData.resultCheck = [](const HttpMqttResult& resultInput) {
        validateStatusCode(resultInput, k_httpStatusNoContent, "disconnect result");
    };

    return requestData;
}

HttpMqttResult buildDisconnectV1Response() {
    HttpMqttResult resultOutput{};
    resultOutput.statusCode = k_httpStatusNoContent;
    resultOutput.headers = makeStandardJsonHeaders();
    resultOutput.headers["version"] = std::string{k_versionValue};
    resultOutput.payload.clear();

    return resultOutput;
}

HttpMqttRequestData buildPublishV1Request(const HttpMqttPublishOptions& optionsInput) {
    HttpMqttRequestData requestData{};
    requestData.headers = makeStandardJsonHeaders();
    requestData.headers["version"] = std::string{k_versionValue};

    const int qosNumber = static_cast<int>(optionsInput.message.qos());
    const bool retainEnabled = optionsInput.message.retain();
    const bool dupEnabled = optionsInput.dup.value_or(false);

    requestData.headers["qos"] = std::to_string(qosNumber);
    requestData.headers["retain"] = retainEnabled ? "1" : "0";
    requestData.headers["dup"] = dupEnabled ? "1" : "0";
    if (optionsInput.packetId.has_value()) {
        requestData.headers["packetid"] = std::to_string(*optionsInput.packetId);
    }

    if (optionsInput.message.rawPayload().has_value()) {
        requestData.payload = *optionsInput.message.rawPayload();
    } else {
        requestData.payload = std::format(
            R"({{"token":"{}","message":{{"topic":"{}","value":{},"reason":{}}}}})",
            escapeJsonString(optionsInput.token),
            escapeJsonString(optionsInput.message.topic()),
            messageValueToJson(optionsInput.message.value()),
            reasonToJson(optionsInput.message));
    }

    requestData.resultCheck = [expectedQos = qosNumber, expectedPacketId = optionsInput.packetId](
                                  const HttpMqttResult& resultInput) {
        validateStatusCode(resultInput, k_httpStatusNoContent, "publish result");
        validatePacketIdMatch(resultInput, expectedPacketId, "publish result");
        if (expectedQos == 1) {
            validateHeaderEquals(resultInput, "packet", "puback", "publish result");
        }
        if (expectedQos == 2) {
            validateHeaderEquals(resultInput, "packet", "pubrec", "publish result");
        }
    };

    return requestData;
}

HttpMqttResult buildPublishV1Response(const HttpMqttHeaders& headersInput) {
    HttpMqttResult resultOutput{};
    resultOutput.statusCode = k_httpStatusNoContent;
    resultOutput.headers = makeStandardJsonHeaders();
    resultOutput.headers["version"] = std::string{k_versionValue};
    resultOutput.payload.clear();

    const auto qosHeader = tryReadHeaderValue(headersInput, "qos");
    const auto retainHeader = tryReadHeaderValue(headersInput, "retain");
    if (qosHeader.has_value()) {
        resultOutput.headers["qos"] = *qosHeader;
    }
    if (retainHeader.has_value()) {
        resultOutput.headers["retain"] = *retainHeader;
    }

    const auto packetId = readPacketIdHeader(headersInput);
    if (packetId.has_value()) {
        resultOutput.packetId = packetId;
        resultOutput.headers["packetid"] = std::to_string(*packetId);
    }

    int qosNumber = 0;
    if (qosHeader.has_value()) {
        const auto parseResult = std::from_chars(qosHeader->data(), qosHeader->data() + qosHeader->size(), qosNumber);
        if (parseResult.ec != std::errc{} || parseResult.ptr != qosHeader->data() + qosHeader->size()) {
            qosNumber = 0;
        }
    }

    if (qosNumber == 1) {
        resultOutput.headers["packet"] = "puback";
    } else if (qosNumber == 2) {
        resultOutput.headers["packet"] = "pubrec";
    }

    return resultOutput;
}

HttpMqttRequestData buildPubrelV1Request(const HttpMqttPubrelOptions& optionsInput) {
    HttpMqttRequestData requestData{};
    requestData.headers = makeStandardTextHeaders();
    requestData.headers["version"] = std::string{k_versionValue};
    if (optionsInput.packetId.has_value()) {
        requestData.headers["packetid"] = std::to_string(*optionsInput.packetId);
    }

    requestData.payload = std::format(R"({{"token":"{}"}})", escapeJsonString(optionsInput.token));
    requestData.resultCheck = [expectedPacketId = optionsInput.packetId](const HttpMqttResult& resultInput) {
        validateStatusCode(resultInput, k_httpStatusNoContent, "pubrel result");
        validatePacketIdMatch(resultInput, expectedPacketId, "pubrel result");
        validateHeaderEquals(resultInput, "packet", "pubcomp", "pubrel result");
    };

    return requestData;
}

HttpMqttResult buildPubrelV1Response(const HttpMqttHeaders& headersInput) {
    HttpMqttResult resultOutput{};
    resultOutput.statusCode = k_httpStatusNoContent;
    resultOutput.headers = makeStandardJsonHeaders();
    resultOutput.headers["version"] = std::string{k_versionValue};
    resultOutput.headers["packet"] = "pubcomp";
    resultOutput.payload.clear();

    const auto packetId = readPacketIdHeader(headersInput);
    if (packetId.has_value()) {
        resultOutput.packetId = packetId;
        resultOutput.headers["packetid"] = std::to_string(*packetId);
    }

    return resultOutput;
}

} // namespace yaha
