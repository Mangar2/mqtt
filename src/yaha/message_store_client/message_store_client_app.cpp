#include "yaha/message_store_client/message_store_client_app.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace yaha {

namespace {

using KeyValueMap = std::unordered_map<std::string, std::string>;
using SectionMap = std::unordered_map<std::string, KeyValueMap>;

std::string trimCopy(std::string value) {
    auto notSpace = [](unsigned char character) {
        return std::isspace(character) == 0;
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string stripComment(std::string line) {
    const std::size_t semicolonPosition = line.find(';');
    const std::size_t commentPosition =
        semicolonPosition == std::string::npos ? line.size() : semicolonPosition;
    return line.substr(0U, commentPosition);
}

bool tryParseUnsigned(const std::string& text,
                      std::uint64_t minValue,
                      std::uint64_t maxValue,
                      std::uint64_t& output) {
    if (text.empty()) {
        return false;
    }

    char* endPointer = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &endPointer, 10);
    if (endPointer == nullptr || *endPointer != '\0') {
        return false;
    }

    if (parsed < minValue || parsed > maxValue) {
        return false;
    }

    output = static_cast<std::uint64_t>(parsed);
    return true;
}

bool tryParseSectionMap(const std::filesystem::path& configPath,
                        SectionMap& sections,
                        std::string& errorMessage) {
    std::ifstream input{configPath};
    if (!input.is_open()) {
        errorMessage = "unable to open config file: " + configPath.string();
        return false;
    }

    std::string currentSection{};
    std::string line{};
    std::uint32_t lineNumber = 0U;

    while (std::getline(input, line)) {
        lineNumber += 1U;

        std::string cleaned = trimCopy(stripComment(std::move(line)));
        if (cleaned.empty()) {
            continue;
        }

        if (cleaned.front() == '[' && cleaned.back() == ']') {
            currentSection = trimCopy(cleaned.substr(1U, cleaned.size() - 2U));
            if (currentSection.empty()) {
                errorMessage = "empty section name at line " + std::to_string(lineNumber);
                return false;
            }
            continue;
        }

        const std::size_t delimiterPosition = cleaned.find('=');
        if (delimiterPosition == std::string::npos) {
            errorMessage = "missing '=' at line " + std::to_string(lineNumber);
            return false;
        }

        const std::string key = trimCopy(cleaned.substr(0U, delimiterPosition));
        const std::string value = trimCopy(cleaned.substr(delimiterPosition + 1U));

        if (key.empty()) {
            errorMessage = "empty key at line " + std::to_string(lineNumber);
            return false;
        }

        sections[currentSection][key] = value;
    }

    return true;
}

std::optional<std::string> lookupValue(const SectionMap& sections,
                                       std::string_view section,
                                       std::string_view key) {
    const auto sectionIterator = sections.find(std::string{section});
    if (sectionIterator == sections.end()) {
        return std::nullopt;
    }

    const auto keyIterator = sectionIterator->second.find(std::string{key});
    if (keyIterator == sectionIterator->second.end()) {
        return std::nullopt;
    }

    return keyIterator->second;
}

bool parseSubscriptions(const SectionMap& sections,
                        SubscriptionMap& subscriptions,
                        std::string& errorMessage) {
    const auto sectionIterator = sections.find("subscriptions");
    if (sectionIterator == sections.end() || sectionIterator->second.empty()) {
        subscriptions = {{"#", Qos::AtLeastOnce}};
        return true;
    }

    for (const auto& entry : sectionIterator->second) {
        std::uint64_t qosValue = 0U;
        if (!tryParseUnsigned(entry.second, 0U, 2U, qosValue)) {
            errorMessage = "invalid qos for subscription '" + entry.first + "'";
            return false;
        }

        subscriptions[entry.first] = static_cast<Qos>(qosValue);
    }

    return true;
}

bool parseMessageStoreConfig(const SectionMap& sections,
                             MessageStoreConfig& output,
                             std::string& errorMessage) {
    if (!parseSubscriptions(sections, output.subscriptions, errorMessage)) {
        return false;
    }

    if (const auto cleanupTopic = lookupValue(sections, "messagestore", "cleanupTopic");
        cleanupTopic.has_value()) {
        output.cleanupTopic = *cleanupTopic;
    }

    if (const auto serverPath = lookupValue(sections, "server", "path"); serverPath.has_value()) {
        output.serverPath = *serverPath;
    }

    if (const auto serverPort = lookupValue(sections, "server", "port"); serverPort.has_value()) {
        std::uint64_t parsed = 0U;
        if (!tryParseUnsigned(*serverPort, 0U, 65535U, parsed)) {
            errorMessage = "invalid server.port";
            return false;
        }
        output.serverPort = static_cast<std::uint16_t>(parsed);
    }

    if (const auto persistDirectory = lookupValue(sections, "persist", "directory");
        persistDirectory.has_value()) {
        output.persistenceConfig.directory = *persistDirectory;
    }

    if (const auto persistFilename = lookupValue(sections, "persist", "filename");
        persistFilename.has_value()) {
        output.persistenceConfig.filename = *persistFilename;
    }

    if (const auto persistInterval = lookupValue(sections, "persist", "intervalMs");
        persistInterval.has_value()) {
        std::uint64_t parsed = 0U;
        if (!tryParseUnsigned(*persistInterval, 0U, 86400000U, parsed)) {
            errorMessage = "invalid persist.intervalMs";
            return false;
        }
        output.persistenceConfig.intervalMs = static_cast<std::uint32_t>(parsed);
    }

    if (const auto keepFiles = lookupValue(sections, "persist", "keepFiles"); keepFiles.has_value()) {
        std::uint64_t parsed = 0U;
        if (!tryParseUnsigned(*keepFiles, 0U, 1024U, parsed)) {
            errorMessage = "invalid persist.keepFiles";
            return false;
        }
        output.persistenceConfig.keepFiles = static_cast<std::uint32_t>(parsed);
    }

    return true;
}

bool parseMqttConfig(const SectionMap& sections,
                     YahaMqttClient::Config& output,
                     std::string& errorMessage) {
    if (const auto host = lookupValue(sections, "mqtt", "host"); host.has_value()) {
        output.brokerHost = *host;
    }

    if (const auto port = lookupValue(sections, "mqtt", "port"); port.has_value()) {
        std::uint64_t parsed = 0U;
        if (!tryParseUnsigned(*port, 1U, 65535U, parsed)) {
            errorMessage = "invalid mqtt.port";
            return false;
        }
        output.brokerPort = static_cast<std::uint16_t>(parsed);
    }

    if (const auto clientId = lookupValue(sections, "mqtt", "clientId"); clientId.has_value()) {
        output.clientId = *clientId;
    }

    if (const auto reconnectDelayMs = lookupValue(sections, "mqtt", "reconnectDelayMs");
        reconnectDelayMs.has_value()) {
        std::uint64_t parsed = 0U;
        if (!tryParseUnsigned(*reconnectDelayMs, 1U, 600000U, parsed)) {
            errorMessage = "invalid mqtt.reconnectDelayMs";
            return false;
        }
        output.reconnectDelay = std::chrono::milliseconds{parsed};
    }

    if (const auto keepAliveIntervalMs = lookupValue(sections, "mqtt", "keepAliveIntervalMs");
        keepAliveIntervalMs.has_value()) {
        std::uint64_t parsed = 0U;
        if (!tryParseUnsigned(*keepAliveIntervalMs, 1U, 600000U, parsed)) {
            errorMessage = "invalid mqtt.keepAliveIntervalMs";
            return false;
        }
        output.keepAliveInterval = std::chrono::milliseconds{parsed};
    }

    if (const auto loopSleepMs = lookupValue(sections, "mqtt", "loopSleepMs");
        loopSleepMs.has_value()) {
        std::uint64_t parsed = 0U;
        if (!tryParseUnsigned(*loopSleepMs, 1U, 1000U, parsed)) {
            errorMessage = "invalid mqtt.loopSleepMs";
            return false;
        }
        output.loopSleep = std::chrono::milliseconds{parsed};
    }

    return true;
}

} // namespace

MessageStoreClientApp::MessageStoreClientApp(MessageStoreClientRuntimeConfig config)
    : configStore_(std::move(config.storeConfig))
    , mqttClient_(std::move(config.mqttConfig), configStore_, makeInMemoryTransport()) {}

void MessageStoreClientApp::run() {
    configStore_.run();
    mqttClient_.run();
}

void MessageStoreClientApp::close() {
    mqttClient_.close();
    configStore_.close();
}

bool MessageStoreClientApp::isRunning() const {
    return configStore_.isRunning() && mqttClient_.isRunning();
}

bool MessageStoreClientApp::tryLoadConfigFromFile(
    const std::filesystem::path& configPath,
    MessageStoreClientRuntimeConfig& output,
    std::string& errorMessage) {
    SectionMap sections{};
    if (!tryParseSectionMap(configPath, sections, errorMessage)) {
        return false;
    }

    MessageStoreClientRuntimeConfig parsed{};
    if (!parseMessageStoreConfig(sections, parsed.storeConfig, errorMessage)) {
        return false;
    }
    if (!parseMqttConfig(sections, parsed.mqttConfig, errorMessage)) {
        return false;
    }

    output = std::move(parsed);
    return true;
}

YahaMqttClient::Transport MessageStoreClientApp::makeInMemoryTransport() {
    struct State {
        std::atomic<bool> connected{false};
    };

    auto sharedState = std::make_shared<State>();

    YahaMqttClient::Transport transport{};
    transport.connect = [sharedState](const YahaMqttClient::Config&) {
        sharedState->connected.store(true);
        return true;
    };
    transport.disconnect = [sharedState]() {
        sharedState->connected.store(false);
    };
    transport.publish = [](const Message&) {};
    transport.subscribe = [](const std::string&, Qos) {};
    transport.pollIncoming = []() -> std::optional<Message> {
        return std::nullopt;
    };
    transport.ping = []() {};
    transport.isConnected = [sharedState]() {
        return sharedState->connected.load();
    };

    return transport;
}

} // namespace yaha
