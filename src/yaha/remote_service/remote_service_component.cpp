#include "yaha/remote_service/remote_service_component.h"

#include "httplib.h"

#include <cctype>
#include <cstdint>
#include <exception>
#include <format>
#include <iostream>
#include <string>
#include <utility>

namespace yaha {

namespace {

constexpr int kHttpStatusOk{200};

void skipWhitespace(const std::string& textValue, std::size_t& parseIndex) {
    while (parseIndex < textValue.size()
        && std::isspace(static_cast<unsigned char>(textValue[parseIndex])) != 0) {
        parseIndex += 1U;
    }
}

[[nodiscard]] bool consumeChar(
    const std::string& textValue,
    std::size_t& parseIndex,
    const char expectedChar) {
    skipWhitespace(textValue, parseIndex);
    if (parseIndex >= textValue.size() || textValue[parseIndex] != expectedChar) {
        return false;
    }
    parseIndex += 1U;
    return true;
}

[[nodiscard]] bool parseJsonString(
    const std::string& textValue,
    std::size_t& parseIndex,
    std::string& output) {
    skipWhitespace(textValue, parseIndex);
    if (parseIndex >= textValue.size() || textValue[parseIndex] != '"') {
        return false;
    }
    parseIndex += 1U;

    std::string parsedValue{};
    while (parseIndex < textValue.size()) {
        const char currentChar = textValue[parseIndex++];
        if (currentChar == '"') {
            output = std::move(parsedValue);
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
                parsedValue.push_back(escapedChar);
                break;
            case 'n':
                parsedValue.push_back('\n');
                break;
            case 'r':
                parsedValue.push_back('\r');
                break;
            case 't':
                parsedValue.push_back('\t');
                break;
            default:
                return false;
            }
            continue;
        }

        parsedValue.push_back(currentChar);
    }

    return false;
}

[[nodiscard]] bool parseJsonUnsignedInteger(
    const std::string& textValue,
    std::size_t& parseIndex,
    std::uint64_t& output) {
    skipWhitespace(textValue, parseIndex);
    if (parseIndex >= textValue.size()) {
        return false;
    }

    std::size_t tokenEnd = parseIndex;
    while (tokenEnd < textValue.size() && std::isdigit(static_cast<unsigned char>(textValue[tokenEnd])) != 0) {
        tokenEnd += 1U;
    }

    if (tokenEnd == parseIndex) {
        return false;
    }

    const std::string numberText = textValue.substr(parseIndex, tokenEnd - parseIndex);
    try {
        output = static_cast<std::uint64_t>(std::stoull(numberText));
    } catch (...) {
        return false;
    }

    parseIndex = tokenEnd;
    return true;
}

[[nodiscard]] bool skipJsonValue(const std::string& payloadText, std::size_t& parseIndex);

[[nodiscard]] bool skipJsonObject(const std::string& payloadText, std::size_t& parseIndex) {
    if (!consumeChar(payloadText, parseIndex, '{')) {
        return false;
    }

    while (true) {
        skipWhitespace(payloadText, parseIndex);
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == '}') {
            parseIndex += 1U;
            return true;
        }

        std::string keyName{};
        if (!parseJsonString(payloadText, parseIndex, keyName)) {
            return false;
        }

        if (!consumeChar(payloadText, parseIndex, ':')) {
            return false;
        }

        if (!skipJsonValue(payloadText, parseIndex)) {
            return false;
        }

        skipWhitespace(payloadText, parseIndex);
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == ',') {
            parseIndex += 1U;
            continue;
        }

        if (parseIndex < payloadText.size() && payloadText[parseIndex] == '}') {
            parseIndex += 1U;
            return true;
        }
        return false;
    }
}

[[nodiscard]] bool skipJsonArray(const std::string& payloadText, std::size_t& parseIndex) {
    if (!consumeChar(payloadText, parseIndex, '[')) {
        return false;
    }

    while (true) {
        skipWhitespace(payloadText, parseIndex);
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == ']') {
            parseIndex += 1U;
            return true;
        }

        if (!skipJsonValue(payloadText, parseIndex)) {
            return false;
        }

        skipWhitespace(payloadText, parseIndex);
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == ',') {
            parseIndex += 1U;
            continue;
        }

        if (parseIndex < payloadText.size() && payloadText[parseIndex] == ']') {
            parseIndex += 1U;
            return true;
        }
        return false;
    }
}

[[nodiscard]] bool skipLiteralToken(
    const std::string& payloadText,
    std::size_t& parseIndex,
    const std::string_view literalText) {
    skipWhitespace(payloadText, parseIndex);
    if (payloadText.compare(parseIndex, literalText.size(), literalText) != 0) {
        return false;
    }

    parseIndex += literalText.size();
    return true;
}

[[nodiscard]] bool skipJsonNumber(const std::string& payloadText, std::size_t& parseIndex) {
    skipWhitespace(payloadText, parseIndex);
    if (parseIndex >= payloadText.size()) {
        return false;
    }

    std::size_t tokenEnd = parseIndex;
    if (payloadText[tokenEnd] == '-') {
        tokenEnd += 1U;
    }

    bool hasDigit = false;
    while (tokenEnd < payloadText.size() && std::isdigit(static_cast<unsigned char>(payloadText[tokenEnd])) != 0) {
        hasDigit = true;
        tokenEnd += 1U;
    }

    if (tokenEnd < payloadText.size() && payloadText[tokenEnd] == '.') {
        tokenEnd += 1U;
        while (tokenEnd < payloadText.size()
            && std::isdigit(static_cast<unsigned char>(payloadText[tokenEnd])) != 0) {
            hasDigit = true;
            tokenEnd += 1U;
        }
    }

    if (!hasDigit) {
        return false;
    }

    parseIndex = tokenEnd;
    return true;
}

[[nodiscard]] bool skipJsonValue(const std::string& payloadText, std::size_t& parseIndex) {
    skipWhitespace(payloadText, parseIndex);
    if (parseIndex >= payloadText.size()) {
        return false;
    }

    const char currentChar = payloadText[parseIndex];
    if (currentChar == '"') {
        std::string ignoredValue{};
        return parseJsonString(payloadText, parseIndex, ignoredValue);
    }

    if (currentChar == '{') {
        return skipJsonObject(payloadText, parseIndex);
    }

    if (currentChar == '[') {
        return skipJsonArray(payloadText, parseIndex);
    }

    if (currentChar == '-' || std::isdigit(static_cast<unsigned char>(currentChar)) != 0) {
        return skipJsonNumber(payloadText, parseIndex);
    }

    if (currentChar == 't') {
        return skipLiteralToken(payloadText, parseIndex, "true");
    }

    if (currentChar == 'f') {
        return skipLiteralToken(payloadText, parseIndex, "false");
    }

    if (currentChar == 'n') {
        return skipLiteralToken(payloadText, parseIndex, "null");
    }

    return false;
}

[[nodiscard]] bool parseDevicesObject(
    const std::string& payloadText,
    std::size_t& parseIndex,
    std::map<std::string, std::string>& output) {
    if (!consumeChar(payloadText, parseIndex, '{')) {
        return false;
    }

    std::map<std::string, std::string> parsed{};
    while (true) {
        skipWhitespace(payloadText, parseIndex);
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == '}') {
            parseIndex += 1U;
            output = std::move(parsed);
            return true;
        }

        std::string deviceId{};
        if (!parseJsonString(payloadText, parseIndex, deviceId)) {
            return false;
        }

        if (!consumeChar(payloadText, parseIndex, ':')) {
            return false;
        }

        std::string topicName{};
        if (!parseJsonString(payloadText, parseIndex, topicName)) {
            return false;
        }

        parsed[deviceId] = topicName;

        skipWhitespace(payloadText, parseIndex);
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == ',') {
            parseIndex += 1U;
            continue;
        }

        if (parseIndex < payloadText.size() && payloadText[parseIndex] == '}') {
            parseIndex += 1U;
            output = std::move(parsed);
            return true;
        }

        return false;
    }
}

[[nodiscard]] bool parseServiceField(
    const std::string& payloadText,
    std::size_t& parseIndex,
    const std::string& fieldName,
    std::string& servicePath,
    RemoteServiceServiceMapping& parsedMapping,
    bool& hasPath,
    bool& hasDevices,
    std::string& errorMessage) {
    if (fieldName == "path") {
        if (!parseJsonString(payloadText, parseIndex, servicePath)) {
            errorMessage = "service.path must be string";
            return false;
        }
        hasPath = true;
        return true;
    }

    if (fieldName == "devices") {
        if (!parseDevicesObject(payloadText, parseIndex, parsedMapping.devices)) {
            errorMessage = "service.devices must be object<string,string>";
            return false;
        }
        hasDevices = true;
        return true;
    }

    if (fieldName == "qos") {
        std::uint64_t qosValue = 0U;
        if (!parseJsonUnsignedInteger(payloadText, parseIndex, qosValue) || qosValue > 2U) {
            errorMessage = "service.qos must be integer in range 0..2";
            return false;
        }
        parsedMapping.qos = static_cast<Qos>(qosValue);
        return true;
    }

    if (fieldName == "reason") {
        if (!parseJsonString(payloadText, parseIndex, parsedMapping.reason)) {
            errorMessage = "service.reason must be string";
            return false;
        }
        return true;
    }

    if (!skipJsonValue(payloadText, parseIndex)) {
        errorMessage = std::format("unsupported JSON value for service field '{}'", fieldName);
        return false;
    }

    return true;
}

[[nodiscard]] std::optional<bool> consumeServiceFieldSeparator(
    const std::string& payloadText,
    std::size_t& parseIndex,
    std::string& errorMessage) {
    skipWhitespace(payloadText, parseIndex);
    if (parseIndex < payloadText.size() && payloadText[parseIndex] == ',') {
        parseIndex += 1U;
        return true;
    }

    if (parseIndex < payloadText.size() && payloadText[parseIndex] == '}') {
        parseIndex += 1U;
        return false;
    }

    errorMessage = "service object has invalid separator";
    return std::nullopt;
}

[[nodiscard]] bool parseServiceEntry(
    const std::string& payloadText,
    std::size_t& parseIndex,
    std::string& servicePath,
    RemoteServiceServiceMapping& outputMapping,
    std::string& errorMessage) {
    if (!consumeChar(payloadText, parseIndex, '{')) {
        errorMessage = "service entry must be object";
        return false;
    }

    RemoteServiceServiceMapping parsedMapping{};
    bool hasPath = false;
    bool hasDevices = false;

    while (true) {
        skipWhitespace(payloadText, parseIndex);
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == '}') {
            parseIndex += 1U;
            break;
        }

        std::string fieldName{};
        if (!parseJsonString(payloadText, parseIndex, fieldName)) {
            errorMessage = "service field name must be string";
            return false;
        }

        if (!consumeChar(payloadText, parseIndex, ':')) {
            errorMessage = "service field must contain ':'";
            return false;
        }

        if (!parseServiceField(
                payloadText,
                parseIndex,
                fieldName,
                servicePath,
                parsedMapping,
                hasPath,
                hasDevices,
                errorMessage)) {
            return false;
        }

        const std::optional<bool> hasNextField = consumeServiceFieldSeparator(payloadText, parseIndex, errorMessage);
        if (!hasNextField.has_value()) {
            return false;
        }
        if (!*hasNextField) {
            break;
        }
    }

    if (!hasPath) {
        errorMessage = "service.path is required";
        return false;
    }
    if (servicePath.empty()) {
        errorMessage = "service.path must not be empty";
        return false;
    }
    if (!hasDevices) {
        errorMessage = "service.devices is required";
        return false;
    }

    outputMapping = std::move(parsedMapping);
    return true;
}

[[nodiscard]] bool startsWithText(const std::string& textValue, const std::string& prefix) {
    return textValue.size() >= prefix.size() && textValue.compare(0U, prefix.size(), prefix) == 0;
}

[[nodiscard]] bool parseServicesArray(
    const std::string& payloadText,
    std::size_t& parseIndex,
    RemoteServiceMap& parsedMap,
    std::string& errorMessage) {
    if (!consumeChar(payloadText, parseIndex, '[')) {
        errorMessage = "services must be array";
        return false;
    }

    while (true) {
        skipWhitespace(payloadText, parseIndex);
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == ']') {
            parseIndex += 1U;
            return true;
        }

        std::string servicePath{};
        RemoteServiceServiceMapping serviceMapping{};
        if (!parseServiceEntry(payloadText, parseIndex, servicePath, serviceMapping, errorMessage)) {
            return false;
        }

        if (parsedMap.contains(servicePath)) {
            std::cerr
                << std::format(
                       "remote_service duplicate path '{}' ignored (first occurrence kept)",
                       servicePath)
                << '\n'
                << std::flush;
        } else {
            parsedMap.insert({std::move(servicePath), std::move(serviceMapping)});
        }

        skipWhitespace(payloadText, parseIndex);
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == ',') {
            parseIndex += 1U;
            continue;
        }
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == ']') {
            parseIndex += 1U;
            return true;
        }

        errorMessage = "services array has invalid separator";
        return false;
    }
}

[[nodiscard]] bool parseRootField(
    const std::string& payloadText,
    std::size_t& parseIndex,
    const std::string& fieldName,
    bool& hasServices,
    RemoteServiceMap& parsedMap,
    std::string& errorMessage) {
    if (fieldName == "services") {
        hasServices = true;
        return parseServicesArray(payloadText, parseIndex, parsedMap, errorMessage);
    }

    if (!skipJsonValue(payloadText, parseIndex)) {
        errorMessage = std::format("invalid JSON value for root field '{}'", fieldName);
        return false;
    }

    return true;
}

[[nodiscard]] bool consumeRootFieldSeparator(
    const std::string& payloadText,
    std::size_t& parseIndex,
    std::string& errorMessage) {
    skipWhitespace(payloadText, parseIndex);
    if (parseIndex < payloadText.size() && payloadText[parseIndex] == ',') {
        parseIndex += 1U;
        return true;
    }
    if (parseIndex < payloadText.size() && payloadText[parseIndex] == '}') {
        parseIndex += 1U;
        return false;
    }

    errorMessage = "root object has invalid separator";
    return false;
}

} // namespace

bool tryParseRemoteServiceMappingPayload(
    const std::string& payloadText,
    RemoteServiceMap& output,
    std::string& errorMessage) {
    std::size_t parseIndex = 0U;
    if (!consumeChar(payloadText, parseIndex, '{')) {
        errorMessage = "root payload must be object";
        return false;
    }

    bool hasServices = false;
    RemoteServiceMap parsedMap{};

    while (true) {
        skipWhitespace(payloadText, parseIndex);
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == '}') {
            parseIndex += 1U;
            break;
        }

        std::string fieldName{};
        if (!parseJsonString(payloadText, parseIndex, fieldName)) {
            errorMessage = "root field name must be string";
            return false;
        }

        if (!consumeChar(payloadText, parseIndex, ':')) {
            errorMessage = "root field must contain ':'";
            return false;
        }

        if (!parseRootField(payloadText, parseIndex, fieldName, hasServices, parsedMap, errorMessage)) {
            return false;
        }

        const bool continueRootObject = consumeRootFieldSeparator(payloadText, parseIndex, errorMessage);
        if (!continueRootObject) {
            if (!errorMessage.empty()) {
                return false;
            }
            break;
        }
    }

    skipWhitespace(payloadText, parseIndex);
    if (parseIndex != payloadText.size()) {
        errorMessage = "payload contains trailing characters";
        return false;
    }

    if (!hasServices) {
        errorMessage = "services array is required";
        return false;
    }

    output = std::move(parsedMap);
    return true;
}

std::optional<std::string> tryExtractFileStoreMonitorKeyPath(const std::string& payloadText) {
    std::size_t parseIndex = 0U;
    if (!consumeChar(payloadText, parseIndex, '{')) {
        return std::nullopt;
    }

    while (true) {
        skipWhitespace(payloadText, parseIndex);
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == '}') {
            return std::nullopt;
        }

        std::string fieldName{};
        if (!parseJsonString(payloadText, parseIndex, fieldName)) {
            return std::nullopt;
        }

        if (!consumeChar(payloadText, parseIndex, ':')) {
            return std::nullopt;
        }

        if (fieldName == "keyPath") {
            std::string keyPath{};
            if (!parseJsonString(payloadText, parseIndex, keyPath)) {
                return std::nullopt;
            }
            return keyPath;
        }

        if (!skipJsonValue(payloadText, parseIndex)) {
            return std::nullopt;
        }

        skipWhitespace(payloadText, parseIndex);
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == ',') {
            parseIndex += 1U;
            continue;
        }
        if (parseIndex < payloadText.size() && payloadText[parseIndex] == '}') {
            return std::nullopt;
        }
        return std::nullopt;
    }
}

RemoteServiceComponent::RemoteServiceComponent(RemoteServiceConfig config)
    : config_(std::move(config)) {
}

SubscriptionMap RemoteServiceComponent::getSubscriptions() const {
    SubscriptionMap subscriptions{};
    subscriptions.insert({config_.monitorTopicPrefix + "/#", config_.subscribeQos});
    return subscriptions;
}

void RemoteServiceComponent::handleMessage(const Message& message) {
    if (!isMonitoringTopic(message.topic())) {
        return;
    }

    if (!isMatchingMappingReloadEvent(message)) {
        std::cout << "remote_service reload ignored: monitor event keyPath mismatch\n" << std::flush;
        return;
    }

    std::cout << "remote_service reload trigger matched: keyPath=" << config_.mappingKeyPath << '\n'
              << std::flush;
    (void)reloadMappingFromFileStore("monitor");
}

void RemoteServiceComponent::run() {
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        if (running_) {
            return;
        }
        running_ = true;
    }

    (void)reloadMappingFromFileStore("startup");
}

void RemoteServiceComponent::close() {
    std::lock_guard<std::mutex> lock{stateMutex_};
    running_ = false;
}

void RemoteServiceComponent::setPublishCallback(PublishCallback callback) {
    std::lock_guard<std::mutex> lock{publishCallbackMutex_};
    publishCallback_ = std::move(callback);
}

bool RemoteServiceComponent::isRunning() const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    return running_;
}

std::size_t RemoteServiceComponent::serviceCount() const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    return servicesByPath_.size();
}

bool RemoteServiceComponent::hasServicePath(const std::string& servicePath) const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    return servicesByPath_.contains(servicePath);
}

std::optional<std::string> RemoteServiceComponent::mappedTopicFor(
    const std::string& servicePath,
    const std::string& deviceId) const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    const auto serviceIterator = servicesByPath_.find(servicePath);
    if (serviceIterator == servicesByPath_.end()) {
        return std::nullopt;
    }

    const auto topicIterator = serviceIterator->second.devices.find(deviceId);
    if (topicIterator == serviceIterator->second.devices.end()) {
        return std::nullopt;
    }

    return topicIterator->second;
}

bool RemoteServiceComponent::reloadMappingFromFileStore(const std::string& triggerText) {
    httplib::Client client{config_.fileStoreHost, static_cast<int>(config_.fileStorePort)};
    const auto response = client.Get(config_.mappingKeyPath);
    if (!response || response->status != kHttpStatusOk) {
        const int statusCode = response ? response->status : -1;
        std::cout << "remote_service reload failed: trigger=" << triggerText
                  << " status=" << statusCode << '\n'
                  << std::flush;
        return false;
    }

    RemoteServiceMap parsedMap{};
    std::string errorMessage{};
    if (!tryParseRemoteServiceMappingPayload(response->body, parsedMap, errorMessage)) {
        std::cout << "remote_service reload failed: trigger=" << triggerText
                  << " validation=" << errorMessage << '\n'
                  << std::flush;
        return false;
    }

    const std::size_t loadedServiceCount = parsedMap.size();
    std::lock_guard<std::mutex> lock{stateMutex_};
    servicesByPath_ = std::move(parsedMap);

    std::cout << "remote_service reload success: trigger=" << triggerText
              << " services=" << loadedServiceCount << '\n'
              << std::flush;
    return true;
}

bool RemoteServiceComponent::isMonitoringTopic(const std::string& topicName) const {
    return startsWithText(topicName, config_.monitorTopicPrefix + "/");
}

bool RemoteServiceComponent::isMatchingMappingReloadEvent(const Message& message) const {
    if (!std::holds_alternative<std::string>(message.value())) {
        return false;
    }

    const std::optional<std::string> keyPath =
        tryExtractFileStoreMonitorKeyPath(std::get<std::string>(message.value()));
    return keyPath.has_value() && *keyPath == config_.mappingKeyPath;
}

bool RemoteServiceCommandResult::isSuccess() const {
    return status == RemoteServiceCommandStatus::Success;
}

RemoteServiceCommandResult RemoteServiceComponent::resolveCommand(
    const RemoteServiceCommandRequest& request) const {
    std::lock_guard<std::mutex> lock{stateMutex_};

    const auto serviceIterator = servicesByPath_.find(request.path);
    if (serviceIterator == servicesByPath_.end()) {
        return {.status = RemoteServiceCommandStatus::ServiceNotFound};
    }

    const auto topicIterator = serviceIterator->second.devices.find(request.deviceId);
    if (topicIterator == serviceIterator->second.devices.end()) {
        return {.status = RemoteServiceCommandStatus::ServiceNotFound};
    }

    Message outboundMessage{
        topicIterator->second,
        request.state,
        serviceIterator->second.qos,
        false};
    outboundMessage.addReason(serviceIterator->second.reason);

    return {
        .status = RemoteServiceCommandStatus::Success,
        .resolvedMessage = std::move(outboundMessage)};
}

RemoteServiceCommandResult RemoteServiceComponent::publishCommand(
    const RemoteServiceCommandRequest& request) {
    RemoteServiceCommandResult resolutionResult = resolveCommand(request);
    if (!resolutionResult.isSuccess()) {
        return resolutionResult;
    }

    PublishCallback publishCallback{};
    {
        std::lock_guard<std::mutex> lock{publishCallbackMutex_};
        publishCallback = publishCallback_;
    }

    if (!publishCallback || !resolutionResult.resolvedMessage.has_value()) {
        std::cout << "remote_service[error] op=publish_command reason=callback_missing"
                  << " path=" << request.path
                  << " deviceId=" << request.deviceId
                  << '\n' << std::flush;
        return {
            .status = RemoteServiceCommandStatus::PublishFailed,
            .resolvedMessage = resolutionResult.resolvedMessage};
    }

    try {
        const PublishResult publishResult = publishCallback(*resolutionResult.resolvedMessage);
        if (!publishResult.success) {
            std::cout << "remote_service[error] op=publish_command reason=publish_rejected"
                      << " path=" << request.path
                      << " deviceId=" << request.deviceId
                      << " category=" << static_cast<int>(publishResult.category)
                      << " detail=\"" << publishResult.reason << "\""
                      << '\n' << std::flush;
            return {
                .status = RemoteServiceCommandStatus::PublishFailed,
                .resolvedMessage = resolutionResult.resolvedMessage};
        }
    } catch (const std::exception& exceptionValue) {
        std::cout << "remote_service[error] op=publish_command reason=exception"
                  << " path=" << request.path
                  << " deviceId=" << request.deviceId
                  << " detail=\"" << exceptionValue.what() << "\""
                  << '\n' << std::flush;
        return {
            .status = RemoteServiceCommandStatus::PublishFailed,
            .resolvedMessage = resolutionResult.resolvedMessage};
    } catch (...) {
        std::cout << "remote_service[error] op=publish_command reason=exception"
                  << " path=" << request.path
                  << " deviceId=" << request.deviceId
                  << " detail=\"unknown\""
                  << '\n' << std::flush;
        return {
            .status = RemoteServiceCommandStatus::PublishFailed,
            .resolvedMessage = resolutionResult.resolvedMessage};
    }

    return resolutionResult;
}

} // namespace yaha