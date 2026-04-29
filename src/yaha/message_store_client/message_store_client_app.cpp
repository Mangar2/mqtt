#include "yaha/message_store_client/message_store_client_app.h"

#include "client/connection_negotiator.h"
#include "client_api/client_config.h"
#include "codec/packet/connect_codec.h"
#include "codec/packet/control_codec.h"
#include "codec/packet/publish_codec.h"
#include "codec/packet/subscribe_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "codec/read_buffer.h"
#include "network/stream_buffer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

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

mqtt::QoS toMqttQos(const Qos qos) {
    switch (qos) {
        case Qos::AtMostOnce:
            return mqtt::QoS::AtMostOnce;
        case Qos::AtLeastOnce:
            return mqtt::QoS::AtLeastOnce;
        case Qos::ExactlyOnce:
            return mqtt::QoS::ExactlyOnce;
    }

    return mqtt::QoS::AtMostOnce;
}

Qos toYahaQos(const mqtt::QoS qos) {
    switch (qos) {
        case mqtt::QoS::AtMostOnce:
            return Qos::AtMostOnce;
        case mqtt::QoS::AtLeastOnce:
            return Qos::AtLeastOnce;
        case mqtt::QoS::ExactlyOnce:
            return Qos::ExactlyOnce;
    }

    return Qos::AtMostOnce;
}

Value decodePayloadValue(const mqtt::BinaryData& payload) {
    const std::string text(payload.data.begin(), payload.data.end());
    if (text.empty()) {
        return std::string{};
    }

    std::size_t parsedLength = 0U;
    try {
        const double number = std::stod(text, &parsedLength);
        if (parsedLength == text.size() && std::isfinite(number)) {
            return number;
        }
    } catch (...) {
    }

    return text;
}

std::span<const std::uint8_t> asSpan(const mqtt::WriteBuffer& buffer) {
    return std::span<const std::uint8_t>(buffer.data(), buffer.size());
}

class BrokerTransportAdapter {
public:
    bool connect(const YahaMqttClient::Config& config) {
        std::lock_guard<std::mutex> lock{mutex_};

        disconnectLocked();

        mqtt::ClientConfig clientConfig{};
        clientConfig.broker_host = config.brokerHost;
        clientConfig.broker_port = config.brokerPort;
        clientConfig.client_id = config.clientId;
        const auto keepAliveSeconds =
            std::chrono::duration_cast<std::chrono::seconds>(config.keepAliveInterval).count();
        clientConfig.keep_alive_seconds = keepAliveSeconds <= 0
            ? 1U
            : static_cast<std::uint16_t>(
                std::min<std::int64_t>(keepAliveSeconds, std::numeric_limits<std::uint16_t>::max()));

        const mqtt::ConnectPacket connectPacket = mqtt::build_connect_packet(clientConfig);

        connection_ = std::make_unique<mqtt::TcpConnection>(
            mqtt::ConnectionNegotiator::dial_tcp(config.brokerHost, config.brokerPort));
        (void)mqtt::ConnectionNegotiator::negotiate(*connection_, connectPacket, 5000U);

        streamBuffer_ = mqtt::StreamBuffer{};
        connected_ = true;
        return true;
    }

    void disconnect() {
        std::lock_guard<std::mutex> lock{mutex_};
        disconnectLocked();
    }

    void publish(const Message& message) {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!connected_ || connection_ == nullptr) {
            return;
        }

        mqtt::PublishPacket packet{};
        packet.topic = mqtt::Utf8String{message.topic()};
        packet.payload = std::holds_alternative<std::string>(message.value())
            ? mqtt::BinaryData::from_string(std::get<std::string>(message.value()))
            : mqtt::BinaryData::from_string(std::to_string(std::get<double>(message.value())));
        packet.qos = toMqttQos(message.qos());
        packet.retain = message.retain();
        if (packet.qos != mqtt::QoS::AtMostOnce) {
            packet.packet_id = nextPacketId_++;
            if (nextPacketId_ == 0U) {
                nextPacketId_ = 1U;
            }
        }

        mqtt::WriteBuffer encoded{};
        mqtt::encode_publish(encoded, packet);
        if (!connection_->write(asSpan(encoded))) {
            disconnectLocked();
        }
    }

    void subscribe(const std::string& topicFilter, const Qos qos) {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!connected_ || connection_ == nullptr) {
            return;
        }

        mqtt::SubscribePacket packet{};
        packet.packet_id = nextPacketId_++;
        if (nextPacketId_ == 0U) {
            nextPacketId_ = 1U;
        }
        packet.filters.push_back(mqtt::SubscribeFilter{
            .topic_filter = mqtt::Utf8String{topicFilter},
            .options = mqtt::SubscribeOptions{.max_qos = toMqttQos(qos)}
        });

        mqtt::WriteBuffer encoded{};
        mqtt::encode_subscribe(encoded, packet);
        if (!connection_->write(asSpan(encoded))) {
            disconnectLocked();
            return;
        }

        try {
            const std::optional<mqtt::AnyPacket> maybePacket = readNextPacketLocked(5000U);
            if (!maybePacket.has_value() || !std::holds_alternative<mqtt::SubackPacket>(*maybePacket)) {
                disconnectLocked();
            }
        } catch (...) {
            disconnectLocked();
        }
    }

    std::optional<Message> pollIncoming() {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!connected_ || connection_ == nullptr) {
            return std::nullopt;
        }

        std::optional<mqtt::AnyPacket> maybePacket{};
        try {
            maybePacket = readNextPacketLocked(20U);
        } catch (...) {
            disconnectLocked();
            return std::nullopt;
        }

        if (!maybePacket.has_value()) {
            return std::nullopt;
        }

        if (std::holds_alternative<mqtt::PingrespPacket>(*maybePacket)) {
            return std::nullopt;
        }

        if (std::holds_alternative<mqtt::PubrelPacket>(*maybePacket)) {
            mqtt::PubcompPacket pubcomp{};
            pubcomp.packet_id = std::get<mqtt::PubrelPacket>(*maybePacket).packet_id;
            mqtt::WriteBuffer encoded{};
            mqtt::encode_pubcomp(encoded, pubcomp);
            if (!connection_->write(asSpan(encoded))) {
                disconnectLocked();
            }
            return std::nullopt;
        }

        if (!std::holds_alternative<mqtt::PublishPacket>(*maybePacket)) {
            return std::nullopt;
        }

        const mqtt::PublishPacket& packet = std::get<mqtt::PublishPacket>(*maybePacket);

        if (packet.qos == mqtt::QoS::AtLeastOnce && packet.packet_id.has_value()) {
            mqtt::PubackPacket puback{};
            puback.packet_id = *packet.packet_id;
            mqtt::WriteBuffer encoded{};
            mqtt::encode_puback(encoded, puback);
            if (!connection_->write(asSpan(encoded))) {
                disconnectLocked();
            }
        } else if (packet.qos == mqtt::QoS::ExactlyOnce && packet.packet_id.has_value()) {
            mqtt::PubrecPacket pubrec{};
            pubrec.packet_id = *packet.packet_id;
            mqtt::WriteBuffer encoded{};
            mqtt::encode_pubrec(encoded, pubrec);
            if (!connection_->write(asSpan(encoded))) {
                disconnectLocked();
            }
        }

        return Message{
            packet.topic.value,
            decodePayloadValue(packet.payload),
            toYahaQos(packet.qos),
            packet.retain
        };
    }

    void ping() {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!connected_ || connection_ == nullptr) {
            return;
        }

        mqtt::WriteBuffer encoded{};
        mqtt::encode_pingreq(encoded);
        if (!connection_->write(asSpan(encoded))) {
            disconnectLocked();
        }
    }

    bool isConnected() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return connected_ && connection_ != nullptr && connection_->is_open();
    }

private:
    std::optional<mqtt::AnyPacket> readNextPacketLocked(const std::uint32_t timeoutMs) {
        if (connection_ == nullptr) {
            return std::nullopt;
        }

        std::array<std::uint8_t, 4096U> receiveBuffer{};

        while (true) {
            if (streamBuffer_.has_complete_packet()) {
                const std::vector<std::uint8_t> packetBytes = streamBuffer_.consume_packet();
                mqtt::ReadBuffer readBuffer{
                    std::span<const std::uint8_t>(packetBytes.data(), packetBytes.size())
                };
                return mqtt::read_packet(readBuffer);
            }

            connection_->set_receive_timeout(timeoutMs);
            const std::ptrdiff_t bytesRead = connection_->read(receiveBuffer);
            if (bytesRead == 0) {
                disconnectLocked();
                return std::nullopt;
            }
            if (bytesRead < 0) {
                if (connection_->last_read_timed_out()) {
                    return std::nullopt;
                }
                disconnectLocked();
                return std::nullopt;
            }

            (void)streamBuffer_.append(std::span<const std::uint8_t>(
                receiveBuffer.data(), static_cast<std::size_t>(bytesRead)));
        }
    }

    void disconnectLocked() {
        if (connection_ != nullptr && connection_->is_open()) {
            mqtt::WriteBuffer encoded{};
            mqtt::DisconnectPacket packet{};
            packet.reason_code = mqtt::ReasonCode::Success;
            mqtt::encode_disconnect(encoded, packet);
            (void)connection_->write(asSpan(encoded));
            connection_->close();
        }

        connection_.reset();
        connected_ = false;
        streamBuffer_ = mqtt::StreamBuffer{};
    }

    mutable std::mutex mutex_{};
    std::unique_ptr<mqtt::TcpConnection> connection_{};
    mqtt::StreamBuffer streamBuffer_{};
    bool connected_{false};
    std::uint16_t nextPacketId_{1U};
};

} // namespace

MessageStoreClientApp::MessageStoreClientApp(MessageStoreClientRuntimeConfig config)
    : configStore_(std::move(config.storeConfig))
    , mqttClient_(std::move(config.mqttConfig), configStore_, makeBrokerTransport()) {}

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

YahaMqttClient::Transport MessageStoreClientApp::makeBrokerTransport() {
    auto adapter = std::make_shared<BrokerTransportAdapter>();

    YahaMqttClient::Transport transport{};
    transport.connect = [adapter](const YahaMqttClient::Config& config) {
        return adapter->connect(config);
    };
    transport.disconnect = [adapter]() {
        adapter->disconnect();
    };
    transport.publish = [adapter](const Message& message) {
        adapter->publish(message);
    };
    transport.subscribe = [adapter](const std::string& topicFilter, const Qos qos) {
        adapter->subscribe(topicFilter, qos);
    };
    transport.pollIncoming = [adapter]() -> std::optional<Message> {
        return adapter->pollIncoming();
    };
    transport.ping = [adapter]() {
        adapter->ping();
    };
    transport.isConnected = [adapter]() {
        return adapter->isConnected();
    };

    return transport;
}

} // namespace yaha
