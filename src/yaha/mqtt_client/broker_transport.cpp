#include "yaha/mqtt_client/broker_transport.h"

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
#include <chrono>
#include <cmath>
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
        (void)mqtt::ConnectionNegotiator::negotiate(*connection_, connectPacket, 5000U);

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
            const std::optional<mqtt::AnyPacket> maybePacket = readNextPacketLocked(5000U);
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
            const std::optional<mqtt::AnyPacket> maybePacket = readNextPacketLocked(5000U);
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
