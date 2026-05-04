#include "yaha/http_mqtt_interface/http_mqtt_interface_contracts.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <format>
#include <stdexcept>

namespace yaha {

namespace {

constexpr std::string_view k_contentTypeName{"content-type"};
constexpr std::string_view k_acceptName{"accept"};
constexpr std::string_view k_acceptCharsetName{"accept-charset"};
constexpr std::string_view k_contentTypeJson{"application/json; charset=UTF-8"};
constexpr std::string_view k_contentTypeText{"text/plain; charset=UTF-8"};
constexpr std::string_view k_acceptDefault{"application/json,text/plain"};
constexpr std::string_view k_acceptCharsetUtf8{"UTF-8"};
constexpr std::string_view k_packetIdName{"packetid"};
constexpr unsigned int k_packetIdMaxValue{65535U};

[[nodiscard]] std::string toLowerCopy(std::string_view valueText) {
    std::string loweredText{valueText};
    std::transform(
        loweredText.begin(),
        loweredText.end(),
        loweredText.begin(),
        [](const unsigned char characterValue) {
            return static_cast<char>(std::tolower(characterValue));
        });
    return loweredText;
}

[[nodiscard]] std::string trimCopy(std::string_view valueText) {
    std::size_t firstIndex = 0U;
    while (firstIndex < valueText.size() &&
           std::isspace(static_cast<unsigned char>(valueText[firstIndex])) != 0) {
        ++firstIndex;
    }

    std::size_t lastIndex = valueText.size();
    while (lastIndex > firstIndex &&
           std::isspace(static_cast<unsigned char>(valueText[lastIndex - 1U])) != 0) {
        --lastIndex;
    }

    return std::string{valueText.substr(firstIndex, lastIndex - firstIndex)};
}

} // namespace

HttpMqttHeaders makeStandardJsonHeaders() {
    return HttpMqttHeaders{
        {std::string{k_contentTypeName}, std::string{k_contentTypeJson}},
        {std::string{k_acceptName}, std::string{k_acceptDefault}},
        {std::string{k_acceptCharsetName}, std::string{k_acceptCharsetUtf8}}};
}

HttpMqttHeaders makeStandardTextHeaders() {
    return HttpMqttHeaders{
        {std::string{k_contentTypeName}, std::string{k_contentTypeText}},
        {std::string{k_acceptName}, std::string{k_acceptDefault}},
        {std::string{k_acceptCharsetName}, std::string{k_acceptCharsetUtf8}}};
}

HttpMqttHeaders normalizeHeaderKeys(const HttpMqttHeaders& headersInput) {
    HttpMqttHeaders normalizedHeaders{};
    for (const auto& [keyText, valueText] : headersInput) {
        normalizedHeaders[toLowerCopy(keyText)] = valueText;
    }
    return normalizedHeaders;
}

std::optional<std::string> tryReadHeaderValue(
    const HttpMqttHeaders& headersInput,
    const std::string_view headerName) {
    const HttpMqttHeaders normalizedHeaders = normalizeHeaderKeys(headersInput);
    const auto iterator = normalizedHeaders.find(toLowerCopy(headerName));
    if (iterator == normalizedHeaders.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

bool headerValueStartsWith(
    const HttpMqttHeaders& headersInput,
    const std::string_view headerName,
    const std::string_view valuePrefix) {
    const auto headerValue = tryReadHeaderValue(headersInput, headerName);
    if (!headerValue.has_value()) {
        return false;
    }

    return headerValue->starts_with(valuePrefix);
}

std::string requireHeaderValue(
    const HttpMqttHeaders& headersInput,
    const std::string_view headerName) {
    const auto headerValue = tryReadHeaderValue(headersInput, headerName);
    if (!headerValue.has_value()) {
        throw std::runtime_error{std::format("missing required header '{}'", headerName)};
    }
    return *headerValue;
}

std::optional<std::uint16_t> parsePacketId(const std::string_view packetIdText) {
    const std::string cleanedText = trimCopy(packetIdText);
    if (cleanedText.empty()) {
        return std::nullopt;
    }

    unsigned int parsedPacketId = 0U;
    const auto* begin = cleanedText.data();
    const auto* finish = cleanedText.data() + cleanedText.size();
    const auto parseResult = std::from_chars(begin, finish, parsedPacketId);
    if (parseResult.ec != std::errc{} || parseResult.ptr != finish ||
        parsedPacketId > k_packetIdMaxValue) {
        return std::nullopt;
    }

    return static_cast<std::uint16_t>(parsedPacketId);
}

std::optional<std::uint16_t> readPacketIdHeader(const HttpMqttHeaders& headersInput) {
    const auto headerValue = tryReadHeaderValue(headersInput, k_packetIdName);
    if (!headerValue.has_value()) {
        return std::nullopt;
    }

    return parsePacketId(*headerValue);
}

std::string resolveVersion(const HttpMqttHeaders& headersInput, const std::string_view defaultVersion) {
    const auto versionValue = tryReadHeaderValue(headersInput, "version");
    if (!versionValue.has_value() || versionValue->empty()) {
        return std::string{defaultVersion};
    }

    return *versionValue;
}

void requireJsonObjectPayload(const std::string_view payloadText, const std::string_view contextText) {
    const std::string cleanedPayload = trimCopy(payloadText);
    if (cleanedPayload.empty() || cleanedPayload.front() != '{' || cleanedPayload.back() != '}') {
        throw std::runtime_error{std::format("{}: payload must be a JSON object", contextText)};
    }
}

} // namespace yaha
