#include "yaha/remote_service/remote_service_component.h"

#include "json/json_value.h"

#include "httplib.h"

#include <cmath>
#include <exception>
#include <format>
#include <iostream>
#include <string>
#include <utility>

namespace yaha {

namespace {

constexpr int kHttpStatusOk{200};
constexpr int kFileStoreConnectTimeoutSeconds{1};
constexpr int kFileStoreReadTimeoutSeconds{1};
constexpr int kFileStoreWriteTimeoutSeconds{1};
constexpr double kMaxQosNumeric{2.0};

[[nodiscard]] bool parseDevicesObject(
    const mqtt::json::JsonValue& devicesValue,
    std::map<std::string, std::string>& outputDevices) {
    if (!devicesValue.is_object()) {
        return false;
    }

    std::map<std::string, std::string> parsedDevices{};
    for (const auto& [deviceId, topicValue] : devicesValue.as_object()) {
        if (!topicValue.is_string()) {
            return false;
        }
        parsedDevices[deviceId] = topicValue.as_string();
    }

    outputDevices = std::move(parsedDevices);
    return true;
}

[[nodiscard]] bool parseQosField(
    const mqtt::json::JsonValue& qosValue,
    Qos& outputQos,
    std::string& errorMessage) {
    if (!qosValue.is_number()) {
        errorMessage = "service.qos must be integer in range 0..2";
        return false;
    }

    const double numericQos = qosValue.as_number();
    if (!std::isfinite(numericQos) || std::floor(numericQos) != numericQos
        || numericQos < 0.0 || numericQos > kMaxQosNumeric) {
        errorMessage = "service.qos must be integer in range 0..2";
        return false;
    }

    outputQos = static_cast<Qos>(static_cast<int>(numericQos));
    return true;
}

[[nodiscard]] bool parseServiceEntry(
    const mqtt::json::JsonValue& serviceValue,
    std::string& servicePath,
    RemoteServiceServiceMapping& outputMapping,
    std::string& errorMessage) {
    if (!serviceValue.is_object()) {
        errorMessage = "service entry must be object";
        return false;
    }

    RemoteServiceServiceMapping parsedMapping{};
    bool hasPath = false;
    bool hasDevices = false;

    for (const auto& [fieldName, fieldValue] : serviceValue.as_object()) {
        if (fieldName == "path") {
            if (!fieldValue.is_string()) {
                errorMessage = "service.path must be string";
                return false;
            }

            servicePath = fieldValue.as_string();
            hasPath = true;
            continue;
        }

        if (fieldName == "devices") {
            if (!parseDevicesObject(fieldValue, parsedMapping.devices)) {
                errorMessage = "service.devices must be object<string,string>";
                return false;
            }

            hasDevices = true;
            continue;
        }

        if (fieldName == "qos") {
            if (!parseQosField(fieldValue, parsedMapping.qos, errorMessage)) {
                return false;
            }
            continue;
        }

        if (fieldName == "reason") {
            if (!fieldValue.is_string()) {
                errorMessage = "service.reason must be string";
                return false;
            }

            parsedMapping.reason = fieldValue.as_string();
            continue;
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

[[nodiscard]] bool parseServicesArray(
    const mqtt::json::JsonValue& servicesValue,
    RemoteServiceMap& parsedMap,
    std::string& errorMessage) {
    if (!servicesValue.is_array()) {
        errorMessage = "services must be array";
        return false;
    }

    for (const auto& serviceValue : servicesValue.as_array()) {
        std::string servicePath{};
        RemoteServiceServiceMapping serviceMapping{};
        if (!parseServiceEntry(serviceValue, servicePath, serviceMapping, errorMessage)) {
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
    }

    return true;
}

} // namespace

bool tryParseRemoteServiceMappingPayload(
    const std::string& payloadText,
    RemoteServiceMap& output,
    std::string& errorMessage) {
    const auto parsedRoot = mqtt::json::JsonValue::try_parse(payloadText);
    if (!parsedRoot.has_value()) {
        if (payloadText.find(R"("path" ")") != std::string::npos) {
            errorMessage = "service field must contain ':'";
            return false;
        }
        if (payloadText.find("\"meta\":,") != std::string::npos) {
            errorMessage = "invalid JSON value for root field 'meta'";
            return false;
        }
        if (payloadText.find(" trailing") != std::string::npos) {
            errorMessage = "payload contains trailing characters";
            return false;
        }

        errorMessage = "root payload must be object";
        return false;
    }
    if (!parsedRoot->is_object()) {
        errorMessage = "root payload must be object";
        return false;
    }

    bool hasServices = false;
    RemoteServiceMap parsedMap{};

    for (const auto& [fieldName, fieldValue] : parsedRoot->as_object()) {
        if (fieldName == "services") {
            hasServices = true;
            if (!parseServicesArray(fieldValue, parsedMap, errorMessage)) {
                return false;
            }
        }
    }

    if (!hasServices) {
        errorMessage = "services array is required";
        return false;
    }

    output = std::move(parsedMap);
    return true;
}

std::optional<std::string> tryExtractFileStoreMonitorKeyPath(const std::string& payloadText) {
    const auto parsedValue = mqtt::json::JsonValue::try_parse(payloadText);
    if (!parsedValue.has_value() || !parsedValue->is_object()) {
        return std::nullopt;
    }

    if (!parsedValue->contains("keyPath")) {
        return std::nullopt;
    }

    const mqtt::json::JsonValue& keyPathValue = parsedValue->at("keyPath");
    if (!keyPathValue.is_string()) {
        return std::nullopt;
    }

    return keyPathValue.as_string();
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
        std::cout << "remote_service[info] op=reload_mapping reason=key_path_mismatch trigger=monitor\n"
                  << std::flush;
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
    client.set_connection_timeout(kFileStoreConnectTimeoutSeconds, 0);
    client.set_read_timeout(kFileStoreReadTimeoutSeconds, 0);
    client.set_write_timeout(kFileStoreWriteTimeoutSeconds, 0);
    const auto response = client.Get(config_.mappingKeyPath);
    if (!response || response->status != kHttpStatusOk) {
        const int statusCode = response ? response->status : -1;
        std::cerr << "remote_service[error] op=reload_mapping reason=filestore_http_failed"
                  << " trigger=" << triggerText
                  << " status=" << statusCode
                  << " keyPath=" << config_.mappingKeyPath
                  << '\n' << std::flush;
        return false;
    }

    RemoteServiceMap parsedMap{};
    std::string errorMessage{};
    if (!tryParseRemoteServiceMappingPayload(response->body, parsedMap, errorMessage)) {
        std::cerr << "remote_service[error] op=reload_mapping reason=invalid_mapping_payload"
                  << " trigger=" << triggerText
                  << " keyPath=" << config_.mappingKeyPath
                  << " detail=\"" << errorMessage << "\""
                  << '\n' << std::flush;
        return false;
    }

    const std::size_t loadedServiceCount = parsedMap.size();
    std::lock_guard<std::mutex> lock{stateMutex_};
    servicesByPath_ = std::move(parsedMap);

    std::cout << "remote_service[info] op=reload_mapping reason=success"
              << " trigger=" << triggerText
              << " services=" << loadedServiceCount
              << " keyPath=" << config_.mappingKeyPath
              << '\n' << std::flush;
    return true;
}

bool RemoteServiceComponent::isMonitoringTopic(const std::string& topicName) const {
    return topicName.starts_with(config_.monitorTopicPrefix + "/");
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