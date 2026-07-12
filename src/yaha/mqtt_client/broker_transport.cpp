#include "yaha/mqtt_client/broker_transport.h"

#include "client/connection_negotiator.h"
#include "client_api/client_config.h"
#include "codec/packet/control_codec.h"
#include "codec/packet/publish_codec.h"
#include "codec/packet/subscribe_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "codec/read_buffer.h"
#include "network/stream_buffer.h"
#include "yaha/message/message_payload_codec.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <span>
#include <string>
#include <vector>

namespace yaha {

namespace {

constexpr std::uint32_t k_connect_timeout_ms{5000U};
constexpr std::uint32_t k_ack_timeout_ms{5000U};
constexpr std::uint32_t k_poll_timeout_ms{20U};
constexpr std::size_t k_receive_buffer_size{4096U};

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

std::span<const std::uint8_t> asSpan(const mqtt::WriteBuffer& buffer) {
    return {buffer.data(), buffer.size()};
}

class BrokerTransportAdapter {
public:
    bool connect(const YahaMqttClient::Config& config) {
        std::lock_guard<std::mutex> lock{transportMutex_};

        disconnectLocked();

        preserveRawEnvelopePayload_ = config.preserveRawEnvelopePayload;

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

        mqtt::ConnectPacket connectPacket = mqtt::build_connect_packet(clientConfig);
        if (config.willEnabled && !config.willTopic.empty()) {
            mqtt::WillData will{};
            will.topic = mqtt::Utf8String{config.willTopic};
            will.payload = mqtt::BinaryData::from_string(buildEnvelopePayload(
                Message{config.willTopic, config.willValue, config.willQos, config.willRetain, false}));
            will.qos = toMqttQos(config.willQos);
            will.retain = config.willRetain;
            connectPacket.will = std::move(will);
        }

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
            throw std::runtime_error{"publish requested while broker transport is disconnected"};
        }

        mqtt::PublishPacket packet{};
        packet.topic = mqtt::Utf8String{message.topic()};
        packet.payload = mqtt::BinaryData::from_string(buildEnvelopePayload(message));
        packet.qos = toMqttQos(message.qos());
        packet.retain = message.retain();
        packet.dup = message.dup() && packet.qos != mqtt::QoS::AtMostOnce;

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
            throw std::runtime_error{"failed to write publish packet to broker"};
        }

        if (packet.qos == mqtt::QoS::AtLeastOnce && packet.packet_id.has_value()) {
            waitForPubackLocked(*packet.packet_id);
            return;
        }

        if (packet.qos == mqtt::QoS::ExactlyOnce && packet.packet_id.has_value()) {
            waitForQos2AckFlowLocked(*packet.packet_id);
        }
    }

    bool subscribe(const std::string& topicFilter, const Qos qos) {
        std::lock_guard<std::mutex> lock{transportMutex_};
        if (!connected_ || connection_ == nullptr) {
            return false;
        }

        mqtt::SubscribePacket packet{};
        packet.packet_id = nextPacketId_++;
        const std::uint16_t expectedPacketId = packet.packet_id;
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
            return false;
        }

        try {
            while (true) {
                const std::optional<mqtt::AnyPacket> maybePacket = readNextPacketLocked(k_ack_timeout_ms);
                if (!maybePacket.has_value()) {
                    disconnectLocked();
                    return false;
                }

                if (std::holds_alternative<mqtt::SubackPacket>(*maybePacket)) {
                    const auto& suback = std::get<mqtt::SubackPacket>(*maybePacket);
                    if (suback.packet_id == expectedPacketId) {
                        return true;
                    }
                    continue;
                }

                if (std::holds_alternative<mqtt::PublishPacket>(*maybePacket)) {
                    if (const std::optional<Message> bufferedMessage =
                            parsePublishPacketLocked(std::get<mqtt::PublishPacket>(*maybePacket));
                        bufferedMessage.has_value()) {
                        pendingIncoming_.push_back(*bufferedMessage);
                    }
                    continue;
                }

                if (std::holds_alternative<mqtt::PingrespPacket>(*maybePacket)) {
                    continue;
                }
            }
        } catch (...) {
            disconnectLocked();
        }

        return false;
    }

    bool unsubscribe(const std::string& topicFilter) {
        std::lock_guard<std::mutex> lock{transportMutex_};
        if (!connected_ || connection_ == nullptr) {
            return false;
        }

        mqtt::UnsubscribePacket packet{};
        packet.packet_id = nextPacketId_++;
        const std::uint16_t expectedPacketId = packet.packet_id;
        if (nextPacketId_ == 0U) {
            nextPacketId_ = 1U;
        }
        packet.topic_filters.push_back(mqtt::Utf8String{topicFilter});

        mqtt::WriteBuffer encoded{};
        mqtt::encode_unsubscribe(encoded, packet);
        if (!connection_->write(asSpan(encoded))) {
            disconnectLocked();
            return false;
        }

        try {
            while (true) {
                const std::optional<mqtt::AnyPacket> maybePacket = readNextPacketLocked(k_ack_timeout_ms);
                if (!maybePacket.has_value()) {
                    disconnectLocked();
                    return false;
                }

                if (std::holds_alternative<mqtt::UnsubackPacket>(*maybePacket)) {
                    const auto& unsuback = std::get<mqtt::UnsubackPacket>(*maybePacket);
                    if (unsuback.packet_id == expectedPacketId) {
                        return true;
                    }
                    continue;
                }

                if (std::holds_alternative<mqtt::PublishPacket>(*maybePacket)) {
                    if (const std::optional<Message> bufferedMessage =
                            parsePublishPacketLocked(std::get<mqtt::PublishPacket>(*maybePacket));
                        bufferedMessage.has_value()) {
                        pendingIncoming_.push_back(*bufferedMessage);
                    }
                    continue;
                }

                if (std::holds_alternative<mqtt::PingrespPacket>(*maybePacket)) {
                    continue;
                }
            }
        } catch (...) {
            disconnectLocked();
        }

        return false;
    }

    std::optional<Message> pollIncoming() {
        std::lock_guard<std::mutex> lock{transportMutex_};
        if (!connected_ || connection_ == nullptr) {
            return std::nullopt;
        }

        if (!pendingIncoming_.empty()) {
            Message bufferedMessage = pendingIncoming_.front();
            pendingIncoming_.pop_front();
            return bufferedMessage;
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

        return parsePublishPacketLocked(std::get<mqtt::PublishPacket>(*maybePacket));
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
    void handleConcurrentPacketWhileWaitingForAckLocked(const mqtt::AnyPacket& packet) {
        if (std::holds_alternative<mqtt::PublishPacket>(packet)) {
            if (const std::optional<Message> bufferedMessage =
                    parsePublishPacketLocked(std::get<mqtt::PublishPacket>(packet));
                bufferedMessage.has_value()) {
                pendingIncoming_.push_back(*bufferedMessage);
            }
            return;
        }

        if (std::holds_alternative<mqtt::PubrelPacket>(packet)) {
            mqtt::PubcompPacket pubcomp{};
            pubcomp.packet_id = std::get<mqtt::PubrelPacket>(packet).packet_id;
            mqtt::WriteBuffer encoded{};
            mqtt::encode_pubcomp(encoded, pubcomp);
            if (!connection_->write(asSpan(encoded))) {
                disconnectLocked();
                throw std::runtime_error{"failed to write PUBCOMP during ack wait"};
            }
        }
    }

    void waitForPubackLocked(const std::uint16_t expectedPacketId) {
        while (true) {
            const std::optional<mqtt::AnyPacket> maybePacket = readNextPacketLocked(k_ack_timeout_ms);
            if (!maybePacket.has_value()) {
                disconnectLocked();
                throw std::runtime_error{"timed out waiting for PUBACK from broker"};
            }

            if (std::holds_alternative<mqtt::PubackPacket>(*maybePacket)) {
                const auto& puback = std::get<mqtt::PubackPacket>(*maybePacket);
                if (puback.packet_id == expectedPacketId) {
                    return;
                }
                continue;
            }

            if (std::holds_alternative<mqtt::PingrespPacket>(*maybePacket)) {
                continue;
            }

            handleConcurrentPacketWhileWaitingForAckLocked(*maybePacket);
        }
    }

    void waitForQos2AckFlowLocked(const std::uint16_t expectedPacketId) {
        while (true) {
            const std::optional<mqtt::AnyPacket> maybePacket = readNextPacketLocked(k_ack_timeout_ms);
            if (!maybePacket.has_value()) {
                disconnectLocked();
                throw std::runtime_error{"timed out waiting for PUBREC from broker"};
            }

            if (std::holds_alternative<mqtt::PubrecPacket>(*maybePacket)) {
                const auto& pubrec = std::get<mqtt::PubrecPacket>(*maybePacket);
                if (pubrec.packet_id == expectedPacketId) {
                    break;
                }
                continue;
            }

            if (std::holds_alternative<mqtt::PingrespPacket>(*maybePacket)) {
                continue;
            }

            handleConcurrentPacketWhileWaitingForAckLocked(*maybePacket);
        }

        mqtt::PubrelPacket pubrel{};
        pubrel.packet_id = expectedPacketId;
        mqtt::WriteBuffer pubrelEncoded{};
        mqtt::encode_pubrel(pubrelEncoded, pubrel);
        if (!connection_->write(asSpan(pubrelEncoded))) {
            disconnectLocked();
            throw std::runtime_error{"failed to write PUBREL packet to broker"};
        }

        while (true) {
            const std::optional<mqtt::AnyPacket> maybePacket = readNextPacketLocked(k_ack_timeout_ms);
            if (!maybePacket.has_value()) {
                disconnectLocked();
                throw std::runtime_error{"timed out waiting for PUBCOMP from broker"};
            }

            if (std::holds_alternative<mqtt::PubcompPacket>(*maybePacket)) {
                const auto& pubcomp = std::get<mqtt::PubcompPacket>(*maybePacket);
                if (pubcomp.packet_id == expectedPacketId) {
                    return;
                }
                continue;
            }

            if (std::holds_alternative<mqtt::PingrespPacket>(*maybePacket)) {
                continue;
            }

            handleConcurrentPacketWhileWaitingForAckLocked(*maybePacket);
        }
    }

    std::optional<Message> parsePublishPacketLocked(const mqtt::PublishPacket& packet) {
        if (packet.qos == mqtt::QoS::AtLeastOnce && packet.packet_id.has_value()) {
            mqtt::PubackPacket puback{};
            puback.packet_id = *packet.packet_id;
            mqtt::WriteBuffer encoded{};
            mqtt::encode_puback(encoded, puback);
            if (!connection_->write(asSpan(encoded))) {
                disconnectLocked();
                return std::nullopt;
            }
        } else if (packet.qos == mqtt::QoS::ExactlyOnce && packet.packet_id.has_value()) {
            mqtt::PubrecPacket pubrec{};
            pubrec.packet_id = *packet.packet_id;
            mqtt::WriteBuffer encoded{};
            mqtt::encode_pubrec(encoded, pubrec);
            if (!connection_->write(asSpan(encoded))) {
                disconnectLocked();
                return std::nullopt;
            }
        }

        const std::string payloadText(packet.payload.data.begin(), packet.payload.data.end());
        const Qos qosLevel = toYahaQos(packet.qos);

        std::optional<Message> forwardedEnvelope =
            parseEnvelopePayload(payloadText,
                                 packet.topic.value,
                                 qosLevel,
                                 packet.retain,
                                 packet.dup);
        if (forwardedEnvelope.has_value()) {
            if (!preserveRawEnvelopePayload_) {
                forwardedEnvelope->clearRawPayload();
            }
            return *forwardedEnvelope;
        }

        return Message{packet.topic.value,
                       decodePayloadValue(packet.payload),
                       qosLevel,
                       packet.retain,
                       packet.dup};
    }

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
        pendingIncoming_.clear();
    }

    mutable std::mutex transportMutex_{};
    std::unique_ptr<mqtt::TcpConnection> connection_{};
    mqtt::StreamBuffer streamBuffer_{};
    std::deque<Message> pendingIncoming_{};
    bool connected_{false};
    std::uint16_t nextPacketId_{1U};
    bool preserveRawEnvelopePayload_{false};
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
        return adapter->subscribe(topicFilter, qos);
    };
    transport.unsubscribe = [adapter](const std::string& topicFilter) {
        return adapter->unsubscribe(topicFilter);
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
