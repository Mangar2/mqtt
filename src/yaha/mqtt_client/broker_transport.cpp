#include "yaha/mqtt_client/broker_transport.h"

#include "client/connection_negotiator.h"
#include "client_api/client_config.h"
#include "codec/packet/control_codec.h"
#include "codec/packet/publish_codec.h"
#include "codec/packet/subscribe_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "codec/read_buffer.h"
#include "network/stream_buffer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace yaha {

namespace {

constexpr std::uint32_t k_connect_timeout_ms{5000U};
constexpr std::uint32_t k_ack_timeout_ms{5000U};
constexpr std::uint32_t k_poll_timeout_ms{20U};
constexpr std::size_t k_receive_buffer_size{4096U};

struct ParsedRange {
    std::size_t start{0U};
    std::size_t end{0U};
};

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
    std::string text(payload.data.begin(), payload.data.end());
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

std::optional<ParsedRange> tryFindObjectRange(const std::string& text,
                                              const std::string& key) {
    const std::string keyToken = "\"" + key + "\"";
    const std::size_t keyPos = text.find(keyToken);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    std::size_t cursorPos = text.find(':', keyPos + keyToken.size());
    if (cursorPos == std::string::npos) {
        return std::nullopt;
    }
    ++cursorPos;

    while (cursorPos < text.size() &&
           std::isspace(static_cast<unsigned char>(text[cursorPos])) != 0) {
        ++cursorPos;
    }
    if (cursorPos >= text.size() || text[cursorPos] != '{') {
        return std::nullopt;
    }

    int depth = 0;
    for (std::size_t indexPos = cursorPos; indexPos < text.size(); ++indexPos) {
        if (text[indexPos] == '{') {
            ++depth;
        } else if (text[indexPos] == '}') {
            --depth;
            if (depth == 0) {
                return ParsedRange{cursorPos, indexPos};
            }
        }
    }

    return std::nullopt;
}

std::optional<ParsedRange> tryFindArrayRange(const std::string& text,
                                             const std::string& key) {
    const std::string keyToken = "\"" + key + "\"";
    const std::size_t keyPos = text.find(keyToken);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    std::size_t cursorPos = text.find(':', keyPos + keyToken.size());
    if (cursorPos == std::string::npos) {
        return std::nullopt;
    }
    ++cursorPos;

    while (cursorPos < text.size() &&
           std::isspace(static_cast<unsigned char>(text[cursorPos])) != 0) {
        ++cursorPos;
    }
    if (cursorPos >= text.size() || text[cursorPos] != '[') {
        return std::nullopt;
    }

    int depth = 0;
    for (std::size_t indexPos = cursorPos; indexPos < text.size(); ++indexPos) {
        if (text[indexPos] == '[') {
            ++depth;
        } else if (text[indexPos] == ']') {
            --depth;
            if (depth == 0) {
                return ParsedRange{cursorPos, indexPos};
            }
        }
    }

    return std::nullopt;
}

std::optional<std::string> tryExtractKeyStringValue(const std::string& objectText,
                                                    const std::string& key) {
    const std::string keyToken = "\"" + key + "\"";
    const std::size_t keyPos = objectText.find(keyToken);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    std::size_t cursorPos = objectText.find(':', keyPos + keyToken.size());
    if (cursorPos == std::string::npos) {
        return std::nullopt;
    }
    ++cursorPos;

    while (cursorPos < objectText.size() &&
           std::isspace(static_cast<unsigned char>(objectText[cursorPos])) != 0) {
        ++cursorPos;
    }
    if (cursorPos >= objectText.size() || objectText[cursorPos] != '"') {
        return std::nullopt;
    }

    ++cursorPos;
    std::string parsedValue{};
    while (cursorPos < objectText.size()) {
        const char currentChar = objectText[cursorPos];
        if (currentChar == '\\') {
            ++cursorPos;
            if (cursorPos >= objectText.size()) {
                return std::nullopt;
            }
            parsedValue.push_back(objectText[cursorPos]);
            ++cursorPos;
            continue;
        }
        if (currentChar == '"') {
            return parsedValue;
        }
        parsedValue.push_back(currentChar);
        ++cursorPos;
    }

    return std::nullopt;
}

std::optional<ParsedRange> tryExtractKeyValueToken(const std::string& objectText,
                                                   const std::string& key) {
    const std::string keyToken = "\"" + key + "\"";
    const std::size_t keyPos = objectText.find(keyToken);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    std::size_t cursorPos = objectText.find(':', keyPos + keyToken.size());
    if (cursorPos == std::string::npos) {
        return std::nullopt;
    }
    ++cursorPos;

    while (cursorPos < objectText.size() &&
           std::isspace(static_cast<unsigned char>(objectText[cursorPos])) != 0) {
        ++cursorPos;
    }
    if (cursorPos >= objectText.size()) {
        return std::nullopt;
    }

    const std::size_t tokenStart = cursorPos;
    if (objectText[cursorPos] == '"') {
        ++cursorPos;
        while (cursorPos < objectText.size()) {
            if (objectText[cursorPos] == '\\') {
                cursorPos += 2U;
                continue;
            }
            if (objectText[cursorPos] == '"') {
                return ParsedRange{tokenStart, cursorPos};
            }
            ++cursorPos;
        }
        return std::nullopt;
    }

    while (cursorPos < objectText.size() && objectText[cursorPos] != ',' && objectText[cursorPos] != '}') {
        ++cursorPos;
    }

    const std::size_t tokenEnd = cursorPos == 0U ? 0U : cursorPos - 1U;
    if (tokenEnd < tokenStart) {
        return std::nullopt;
    }

    return ParsedRange{tokenStart, tokenEnd};
}

std::string trimCopy(const std::string& text) {
    std::size_t begin = 0U;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1U])) != 0) {
        --end;
    }

    return text.substr(begin, end - begin);
}

std::optional<Value> tryParseValueToken(const std::string& objectText,
                                        const ParsedRange tokenRange) {
    const std::size_t tokenStart = tokenRange.start;
    const std::size_t tokenEnd = tokenRange.end;
    if (tokenStart >= objectText.size() || tokenEnd >= objectText.size() || tokenStart > tokenEnd) {
        return std::nullopt;
    }

    if (objectText[tokenStart] == '"') {
        const std::string token = objectText.substr(tokenStart, tokenEnd - tokenStart + 1U);
        const std::optional<std::string> textValue =
            tryExtractKeyStringValue("{\"temp\":" + token + "}", "temp");
        if (!textValue.has_value()) {
            return std::nullopt;
        }
        return Value{*textValue};
    }

    const std::string numericText = trimCopy(objectText.substr(tokenStart, tokenEnd - tokenStart + 1U));
    if (numericText.empty()) {
        return std::nullopt;
    }

    const std::string loweredText = trimCopy(numericText);
    if (loweredText == "true" || loweredText == "false" || loweredText == "null") {
        return Value{loweredText};
    }

    char* parseEnd = nullptr;
    const double parsedNumber = std::strtod(numericText.c_str(), &parseEnd);
    if (parseEnd == nullptr || *parseEnd != '\0' || !std::isfinite(parsedNumber)) {
        return std::nullopt;
    }

    return Value{parsedNumber};
}

std::optional<ReasonEntry> tryParseReasonObject(const std::string& reasonObjectText) {
    const std::optional<std::string> messageText =
        tryExtractKeyStringValue(reasonObjectText, "message");
    if (!messageText.has_value() || messageText->empty()) {
        return std::nullopt;
    }

    const std::optional<std::string> timestampText =
        tryExtractKeyStringValue(reasonObjectText, "timestamp");
    return ReasonEntry{*messageText, timestampText.value_or(std::string{})};
}

std::optional<std::size_t> findReasonObjectEnd(const std::string& reasonArrayText,
                                               const std::size_t objectStart) {
    if (objectStart >= reasonArrayText.size() || reasonArrayText[objectStart] != '{') {
        return std::nullopt;
    }

    int depth = 0;
    for (std::size_t objectEnd = objectStart; objectEnd < reasonArrayText.size(); ++objectEnd) {
        if (reasonArrayText[objectEnd] == '{') {
            ++depth;
        } else if (reasonArrayText[objectEnd] == '}') {
            --depth;
            if (depth == 0) {
                return objectEnd;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::vector<ReasonEntry>> tryParseReasonArray(const std::string& reasonArrayText) {
    if (reasonArrayText.size() < 2U || reasonArrayText.front() != '[' || reasonArrayText.back() != ']') {
        return std::nullopt;
    }

    std::vector<ReasonEntry> reasonEntries{};
    std::size_t cursorPos = 1U;
    while (cursorPos + 1U < reasonArrayText.size()) {
        while (cursorPos + 1U < reasonArrayText.size() &&
               (std::isspace(static_cast<unsigned char>(reasonArrayText[cursorPos])) != 0 ||
                reasonArrayText[cursorPos] == ',')) {
            ++cursorPos;
        }

        if (cursorPos + 1U >= reasonArrayText.size() || reasonArrayText[cursorPos] == ']') {
            break;
        }

        const std::optional<std::size_t> objectEnd =
            findReasonObjectEnd(reasonArrayText, cursorPos);
        if (!objectEnd.has_value()) {
            return std::nullopt;
        }

        const std::string reasonObjectText = reasonArrayText.substr(
            cursorPos, *objectEnd - cursorPos + 1U);
        const std::optional<ReasonEntry> reasonEntry = tryParseReasonObject(reasonObjectText);
        if (!reasonEntry.has_value()) {
            return std::nullopt;
        }

        reasonEntries.push_back(*reasonEntry);
        cursorPos = *objectEnd + 1U;
    }

    return reasonEntries;
}

void appendReasonEntries(const std::vector<ReasonEntry>& reasonEntries,
                         Message& outputMessage) {
    for (std::size_t reverseIndex = reasonEntries.size(); reverseIndex > 0U; --reverseIndex) {
        const ReasonEntry& reasonEntry = reasonEntries[reverseIndex - 1U];
        if (reasonEntry.timestamp.empty()) {
            outputMessage.addReason(reasonEntry.message);
        } else {
            outputMessage.addReason(reasonEntry.message, reasonEntry.timestamp);
        }
    }
}

std::optional<Message> tryParseForwardedEnvelope(const std::string& payloadText,
                                                 const std::string& mqttTopic,
                                                 const Qos qosLevel,
                                                 const bool retainFlag) {
    const std::optional<ParsedRange> messageRange =
        tryFindObjectRange(payloadText, "message");
    if (!messageRange.has_value()) {
        return std::nullopt;
    }

    const std::string bodyText = payloadText.substr(
        messageRange->start, messageRange->end - messageRange->start + 1U);

    const std::optional<std::string> parsedTopic =
        tryExtractKeyStringValue(bodyText, "topic");
    if (!parsedTopic.has_value() || parsedTopic->empty()) {
        return std::nullopt;
    }
    if (*parsedTopic != mqttTopic) {
        return std::nullopt;
    }

    const std::optional<ParsedRange> valueRange =
        tryExtractKeyValueToken(bodyText, "value");
    if (!valueRange.has_value()) {
        return std::nullopt;
    }

    const std::optional<Value> parsedValue =
        tryParseValueToken(bodyText, *valueRange);
    if (!parsedValue.has_value()) {
        return std::nullopt;
    }

    Message parsedMessage{*parsedTopic, *parsedValue, qosLevel, retainFlag};

    const std::optional<ParsedRange> reasonArrayRange =
        tryFindArrayRange(bodyText, "reason");
    if (reasonArrayRange.has_value()) {
        const std::string reasonArray = bodyText.substr(
            reasonArrayRange->start, reasonArrayRange->end - reasonArrayRange->start + 1U);
        const std::optional<std::vector<ReasonEntry>> reasonEntries =
            tryParseReasonArray(reasonArray);
        if (!reasonEntries.has_value()) {
            return std::nullopt;
        }
        appendReasonEntries(*reasonEntries, parsedMessage);
    } else {
        const std::optional<ParsedRange> reasonValueRange =
            tryExtractKeyValueToken(bodyText, "reason");
        if (!reasonValueRange.has_value()) {
            parsedMessage.setRawPayload(payloadText);
            return parsedMessage;
        }

        const std::optional<Value> reasonValue =
            tryParseValueToken(bodyText, *reasonValueRange);
        if (!reasonValue.has_value()) {
            return std::nullopt;
        }
        if (std::holds_alternative<std::string>(*reasonValue)) {
            const std::string& reasonText = std::get<std::string>(*reasonValue);
            if (!reasonText.empty()) {
                parsedMessage.addReason(reasonText);
            }
        }
    }

    parsedMessage.setRawPayload(payloadText);
    return parsedMessage;
}

std::span<const std::uint8_t> asSpan(const mqtt::WriteBuffer& buffer) {
    return std::span<const std::uint8_t>(buffer.data(), buffer.size());
}

class BrokerTransportAdapter {
public:
    bool connect(const YahaMqttClient::Config& config) {
        std::lock_guard<std::mutex> lock{transportMutex_};

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
        (void)mqtt::ConnectionNegotiator::negotiate(*connection_, connectPacket, k_connect_timeout_ms);

        streamBuffer_ = mqtt::StreamBuffer{};
        connected_ = true;
        return true;
    }

    void disconnect() {
        std::lock_guard<std::mutex> lock{transportMutex_};
        disconnectLocked();
    }

    void publish(const Message& message) {
        std::lock_guard<std::mutex> lock{transportMutex_};
        if (!connected_ || connection_ == nullptr) {
            return;
        }

        mqtt::PublishPacket packet{};
        packet.topic = mqtt::Utf8String{message.topic()};
        if (message.rawPayload().has_value()) {
            packet.payload = mqtt::BinaryData::from_string(*message.rawPayload());
        } else {
            packet.payload = std::holds_alternative<std::string>(message.value())
                ? mqtt::BinaryData::from_string(std::get<std::string>(message.value()))
                : mqtt::BinaryData::from_string(std::to_string(std::get<double>(message.value())));
        }
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
        std::lock_guard<std::mutex> lock{transportMutex_};
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
            const std::optional<mqtt::AnyPacket> maybePacket = readNextPacketLocked(k_ack_timeout_ms);
            if (!maybePacket.has_value() || !std::holds_alternative<mqtt::SubackPacket>(*maybePacket)) {
                disconnectLocked();
            }
        } catch (...) {
            disconnectLocked();
        }
    }

    void unsubscribe(const std::string& topicFilter) {
        std::lock_guard<std::mutex> lock{transportMutex_};
        if (!connected_ || connection_ == nullptr) {
            return;
        }

        mqtt::UnsubscribePacket packet{};
        packet.packet_id = nextPacketId_++;
        if (nextPacketId_ == 0U) {
            nextPacketId_ = 1U;
        }
        packet.topic_filters.push_back(mqtt::Utf8String{topicFilter});

        mqtt::WriteBuffer encoded{};
        mqtt::encode_unsubscribe(encoded, packet);
        if (!connection_->write(asSpan(encoded))) {
            disconnectLocked();
            return;
        }

        try {
            const std::optional<mqtt::AnyPacket> maybePacket = readNextPacketLocked(k_ack_timeout_ms);
            if (!maybePacket.has_value() || !std::holds_alternative<mqtt::UnsubackPacket>(*maybePacket)) {
                disconnectLocked();
            }
        } catch (...) {
            disconnectLocked();
        }
    }

    std::optional<Message> pollIncoming() {
        std::lock_guard<std::mutex> lock{transportMutex_};
        if (!connected_ || connection_ == nullptr) {
            return std::nullopt;
        }

        std::optional<mqtt::AnyPacket> maybePacket{};
        try {
            maybePacket = readNextPacketLocked(k_poll_timeout_ms);
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

        const std::string payloadText(packet.payload.data.begin(), packet.payload.data.end());
        const Qos qosLevel = toYahaQos(packet.qos);

        const std::optional<Message> forwardedEnvelope =
            tryParseForwardedEnvelope(payloadText, packet.topic.value, qosLevel, packet.retain);
        if (forwardedEnvelope.has_value()) {
            return *forwardedEnvelope;
        }

        Message incomingMessage{packet.topic.value, decodePayloadValue(packet.payload), qosLevel, packet.retain};
        incomingMessage.setRawPayload(payloadText);
        return incomingMessage;
    }

    void ping() {
        std::lock_guard<std::mutex> lock{transportMutex_};
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
        std::lock_guard<std::mutex> lock{transportMutex_};
        return connected_ && connection_ != nullptr && connection_->is_open();
    }

private:
    std::optional<mqtt::AnyPacket> readNextPacketLocked(const std::uint32_t timeoutMs) {
        if (connection_ == nullptr) {
            return std::nullopt;
        }

        std::array<std::uint8_t, k_receive_buffer_size> receiveBuffer{};

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

    mutable std::mutex transportMutex_{};
    std::unique_ptr<mqtt::TcpConnection> connection_{};
    mqtt::StreamBuffer streamBuffer_{};
    bool connected_{false};
    std::uint16_t nextPacketId_{1U};
};

} // namespace

YahaMqttClient::Transport makeBrokerTransport() {
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
    transport.unsubscribe = [adapter](const std::string& topicFilter) {
        adapter->unsubscribe(topicFilter);
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
