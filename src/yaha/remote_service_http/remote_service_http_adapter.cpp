#include "yaha/remote_service_http/remote_service_http_adapter.h"

#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace yaha {

namespace {

constexpr int kHttpStatusOk{200};
constexpr int kHttpStatusBadRequest{400};
constexpr int kHttpStatusNotFound{404};

std::string trimText(const std::string& textValue) {
    std::size_t firstIndex = 0U;
    while (firstIndex < textValue.size()
        && std::isspace(static_cast<unsigned char>(textValue[firstIndex])) != 0) {
        firstIndex += 1U;
    }

    std::size_t lastIndex = textValue.size();
    while (lastIndex > firstIndex
        && std::isspace(static_cast<unsigned char>(textValue[lastIndex - 1U])) != 0) {
        lastIndex -= 1U;
    }

    return textValue.substr(firstIndex, lastIndex - firstIndex);
}

bool parseJsonString(
    const std::string& textValue,
    std::size_t& parseIndex,
    std::string& output) {
    while (parseIndex < textValue.size()
        && std::isspace(static_cast<unsigned char>(textValue[parseIndex])) != 0) {
        parseIndex += 1U;
    }

    if (parseIndex >= textValue.size() || textValue[parseIndex] != '"') {
        return false;
    }
    parseIndex += 1U;

    std::string parsed{};
    while (parseIndex < textValue.size()) {
        const char currentChar = textValue[parseIndex++];
        if (currentChar == '"') {
            output = std::move(parsed);
            return true;
        }

        if (currentChar == '\\') {
            if (parseIndex >= textValue.size()) {
                return false;
            }

            const char escapedChar = textValue[parseIndex++];
            switch (escapedChar) {
            case '"':
            case '\\':
            case '/':
                parsed.push_back(escapedChar);
                break;
            case 'n':
                parsed.push_back('\n');
                break;
            case 'r':
                parsed.push_back('\r');
                break;
            case 't':
                parsed.push_back('\t');
                break;
            default:
                return false;
            }
            continue;
        }

        parsed.push_back(currentChar);
    }

    return false;
}

bool consumeExpectedChar(const std::string& textValue, std::size_t& parseIndex, const char expectedChar) {
    while (parseIndex < textValue.size()
        && std::isspace(static_cast<unsigned char>(textValue[parseIndex])) != 0) {
        parseIndex += 1U;
    }

    if (parseIndex >= textValue.size() || textValue[parseIndex] != expectedChar) {
        return false;
    }
    parseIndex += 1U;
    return true;
}

bool parseJsonValueToken(
    const std::string& textValue,
    std::size_t& parseIndex,
    std::string& tokenOutput,
    bool& isStringToken) {
    while (parseIndex < textValue.size()
        && std::isspace(static_cast<unsigned char>(textValue[parseIndex])) != 0) {
        parseIndex += 1U;
    }

    if (parseIndex >= textValue.size()) {
        return false;
    }

    if (textValue[parseIndex] == '"') {
        std::string parsedString{};
        if (!parseJsonString(textValue, parseIndex, parsedString)) {
            return false;
        }
        tokenOutput = std::move(parsedString);
        isStringToken = true;
        return true;
    }

    std::size_t tokenEnd = parseIndex;
    while (tokenEnd < textValue.size() && textValue[tokenEnd] != ',' && textValue[tokenEnd] != '}') {
        tokenEnd += 1U;
    }
    if (tokenEnd == parseIndex) {
        return false;
    }

    tokenOutput = trimText(textValue.substr(parseIndex, tokenEnd - parseIndex));
    parseIndex = tokenEnd;
    isStringToken = false;
    return !tokenOutput.empty();
}

void skipWhitespaceCursor(const std::string& payloadText, std::size_t& parseIndex) {
    while (parseIndex < payloadText.size()
        && std::isspace(static_cast<unsigned char>(payloadText[parseIndex])) != 0) {
        parseIndex += 1U;
    }
}

bool parseFlatObjectField(
    const std::string& payloadText,
    std::size_t& parseIndex,
    std::string& keyName,
    std::string& tokenValue,
    bool& isStringToken) {
    if (!parseJsonString(payloadText, parseIndex, keyName)) {
        return false;
    }
    if (!consumeExpectedChar(payloadText, parseIndex, ':')) {
        return false;
    }
    return parseJsonValueToken(payloadText, parseIndex, tokenValue, isStringToken);
}

std::optional<bool> consumeFlatObjectSeparator(const std::string& payloadText, std::size_t& parseIndex) {
    skipWhitespaceCursor(payloadText, parseIndex);
    if (parseIndex < payloadText.size() && payloadText[parseIndex] == ',') {
        parseIndex += 1U;
        return true;
    }
    if (parseIndex < payloadText.size() && payloadText[parseIndex] == '}') {
        parseIndex += 1U;
        return false;
    }
    return std::nullopt;
}

bool parseFlatJsonObject(
    const std::string& payloadText,
    std::unordered_map<std::string, std::pair<std::string, bool>>& output) {
    std::size_t parseIndex = 0U;
    if (!consumeExpectedChar(payloadText, parseIndex, '{')) {
        return false;
    }

    std::unordered_map<std::string, std::pair<std::string, bool>> parsed{};
    while (true) {
        skipWhitespaceCursor(payloadText, parseIndex);
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == '}') {
            parseIndex += 1U;
            break;
        }

        std::string keyName{};
        std::string tokenValue{};
        bool isStringToken = false;
        if (!parseFlatObjectField(payloadText, parseIndex, keyName, tokenValue, isStringToken)) {
            return false;
        }

        parsed[keyName] = {std::move(tokenValue), isStringToken};

        const std::optional<bool> continueObject = consumeFlatObjectSeparator(payloadText, parseIndex);
        if (!continueObject.has_value()) {
            return false;
        }
        if (!*continueObject) {
            break;
        }
    }

    while (parseIndex < payloadText.size()
        && std::isspace(static_cast<unsigned char>(payloadText[parseIndex])) != 0) {
        parseIndex += 1U;
    }
    if (parseIndex != payloadText.size()) {
        return false;
    }

    output = std::move(parsed);
    return true;
}

std::optional<Value> parseStateFromToken(const std::string& tokenValue, const bool isStringToken) {
    if (isStringToken) {
        return Value{tokenValue};
    }

    if (tokenValue == "true" || tokenValue == "false" || tokenValue == "null") {
        return Value{tokenValue};
    }

    char* parseEnd = nullptr;
    const double parsedNumber = std::strtod(tokenValue.c_str(), &parseEnd);
    if (parseEnd == nullptr || *parseEnd != '\0') {
        return std::nullopt;
    }

    return Value{parsedNumber};
}

bool validateToken(
    const RemoteServiceTokenValidator& validator,
    const std::string& tokenValue) {
    if (!validator) {
        return false;
    }
    return validator(tokenValue);
}

} // namespace

RemoteServiceHttpAdapter::RemoteServiceHttpAdapter(RemoteServiceComponent& component)
    : component_(component) {
}

void RemoteServiceHttpAdapter::setAccessTokenValidator(RemoteServiceTokenValidator validator) {
    accessTokenValidator_ = std::move(validator);
}

void RemoteServiceHttpAdapter::setDeviceTokenValidator(RemoteServiceTokenValidator validator) {
    deviceTokenValidator_ = std::move(validator);
}

RemoteServiceHttpResponse RemoteServiceHttpAdapter::handleGet(
    const std::string& servicePath,
    const std::map<std::string, std::string>& queryValues) const {
    const auto deviceIdIterator = queryValues.find("deviceId");
    const auto stateIterator = queryValues.find("state");
    const auto accessTokenIterator = queryValues.find("accessToken");
    if (deviceIdIterator == queryValues.end()
        || stateIterator == queryValues.end()
        || accessTokenIterator == queryValues.end()) {
        return makeBadRequestResponse();
    }

    if (deviceIdIterator->second.empty() || accessTokenIterator->second.empty()) {
        return makeBadRequestResponse();
    }

    if (!validateToken(accessTokenValidator_, accessTokenIterator->second)) {
        return makeBadRequestResponse();
    }

    const RemoteServiceCommandRequest requestData{
        .path = servicePath,
        .deviceId = deviceIdIterator->second,
        .state = Value{stateIterator->second},
        .token = accessTokenIterator->second};

    return publishResolvedRequest(requestData);
}

RemoteServiceHttpResponse RemoteServiceHttpAdapter::handlePost(
    const std::string& servicePath,
    const std::string& payloadText) const {
    std::unordered_map<std::string, std::pair<std::string, bool>> payloadFields{};
    if (!parseFlatJsonObject(payloadText, payloadFields)) {
        return makeBadRequestResponse();
    }

    const auto deviceIdIterator = payloadFields.find("deviceId");
    const auto stateIterator = payloadFields.find("state");
    const auto deviceTokenIterator = payloadFields.find("deviceToken");
    if (deviceIdIterator == payloadFields.end()
        || stateIterator == payloadFields.end()
        || deviceTokenIterator == payloadFields.end()) {
        return makeBadRequestResponse();
    }

    if (!deviceIdIterator->second.second || !deviceTokenIterator->second.second) {
        return makeBadRequestResponse();
    }
    if (deviceIdIterator->second.first.empty() || deviceTokenIterator->second.first.empty()) {
        return makeBadRequestResponse();
    }

    if (!validateToken(deviceTokenValidator_, deviceTokenIterator->second.first)) {
        return makeBadRequestResponse();
    }

    const std::optional<Value> stateValue =
        parseStateFromToken(stateIterator->second.first, stateIterator->second.second);
    if (!stateValue.has_value()) {
        return makeBadRequestResponse();
    }

    const RemoteServiceCommandRequest requestData{
        .path = servicePath,
        .deviceId = deviceIdIterator->second.first,
        .state = *stateValue,
        .token = deviceTokenIterator->second.first};

    return publishResolvedRequest(requestData);
}

RemoteServiceHttpResponse RemoteServiceHttpAdapter::makeOkResponse() {
    return {
        .statusCode = kHttpStatusOk,
        .contentType = "text/plain; charset=UTF-8",
        .payload = "ok"};
}

RemoteServiceHttpResponse RemoteServiceHttpAdapter::makeBadRequestResponse() {
    return {
        .statusCode = kHttpStatusBadRequest,
        .contentType = "text/plain; charset=UTF-8",
        .payload = "Bad request"};
}

RemoteServiceHttpResponse RemoteServiceHttpAdapter::makeNotFoundResponse() {
    return {
        .statusCode = kHttpStatusNotFound,
        .contentType = "text/plain; charset=UTF-8",
        .payload = "Service not found"};
}

RemoteServiceHttpResponse RemoteServiceHttpAdapter::publishResolvedRequest(
    const RemoteServiceCommandRequest& requestData) const {
    const RemoteServiceCommandResult result = component_.publishCommand(requestData);
    if (result.status == RemoteServiceCommandStatus::Success) {
        return makeOkResponse();
    }
    if (result.status == RemoteServiceCommandStatus::ServiceNotFound
        || result.status == RemoteServiceCommandStatus::PublishFailed) {
        return makeNotFoundResponse();
    }

    return makeBadRequestResponse();
}

} // namespace yaha