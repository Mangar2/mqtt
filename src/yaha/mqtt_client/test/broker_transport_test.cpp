#include <catch2/catch_test_macros.hpp>

#include "yaha/mqtt_client/broker_transport.h"

#include "codec/packet/connect_codec.h"
#include "codec/packet/control_codec.h"
#include "codec/packet/publish_codec.h"
#include "codec/packet/subscribe_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "codec/read_buffer.h"
#include "codec/write_buffer.h"
#include "network/stream_buffer.h"
#include "network/tcp_listener.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

class FakeBrokerForTransportTest {
public:
    FakeBrokerForTransportTest() = default;

    ~FakeBrokerForTransportTest() {
        stop();
    }

    void start() {
        listener_.emplace(mqtt::TcpListener::listen(0U));
        running_.store(true);
        accept_thread_ = std::thread([this]() {
            accept_loop();
        });
    }

    void stop() {
        if (!running_.load()) {
            return;
        }

        running_.store(false);
        if (listener_.has_value()) {
            listener_->close();
        }

        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }

        std::lock_guard<std::mutex> lock{client_threads_mutex_};
        for (std::thread& client_thread : client_threads_) {
            if (client_thread.joinable()) {
                client_thread.join();
            }
        }
        client_threads_.clear();
    }

    [[nodiscard]] std::uint16_t port() const {
        if (!listener_.has_value()) {
            return 0U;
        }
        return listener_->port();
    }

private:
    static std::optional<mqtt::AnyPacket> read_next_packet(
        mqtt::TcpConnection& connection,
        mqtt::StreamBuffer& stream_buffer,
        const std::uint32_t timeout_ms) {
        std::array<std::uint8_t, 2048U> read_buffer{};

        while (true) {
            if (stream_buffer.has_complete_packet()) {
                const std::vector<std::uint8_t> packet_bytes =
                    stream_buffer.consume_packet();
                mqtt::ReadBuffer reader{std::span<const std::uint8_t>(
                    packet_bytes.data(), packet_bytes.size())};
                return mqtt::read_packet(reader);
            }

            connection.set_receive_timeout(timeout_ms);
            const std::ptrdiff_t bytes_read = connection.read(read_buffer);
            if (bytes_read == 0) {
                connection.close();
                return std::nullopt;
            }
            if (bytes_read < 0) {
                if (connection.last_read_timed_out()) {
                    return std::nullopt;
                }
                connection.close();
                return std::nullopt;
            }

            (void)stream_buffer.append(std::span<const std::uint8_t>(
                read_buffer.data(), static_cast<std::size_t>(bytes_read)));
        }
    }

    static bool send_connack(mqtt::TcpConnection& connection) {
        mqtt::ConnackPacket connack_packet{};
        connack_packet.reason_code = mqtt::ReasonCode::Success;
        mqtt::WriteBuffer frame{};
        mqtt::encode_connack(frame, connack_packet);
        return connection.write(std::span<const std::uint8_t>(frame.data(), frame.size()));
    }

    static bool send_suback(mqtt::TcpConnection& connection,
                            const std::uint16_t packet_id) {
        mqtt::SubackPacket suback_packet{};
        suback_packet.packet_id = packet_id;
        suback_packet.reason_codes = {mqtt::ReasonCode::GrantedQoS1};
        mqtt::WriteBuffer frame{};
        mqtt::encode_suback(frame, suback_packet);
        return connection.write(std::span<const std::uint8_t>(frame.data(), frame.size()));
    }

    static bool send_unsuback(mqtt::TcpConnection& connection,
                              const std::uint16_t packet_id) {
        mqtt::UnsubackPacket unsuback_packet{};
        unsuback_packet.packet_id = packet_id;
        unsuback_packet.reason_codes = {mqtt::ReasonCode::Success};
        mqtt::WriteBuffer frame{};
        mqtt::encode_unsuback(frame, unsuback_packet);
        return connection.write(std::span<const std::uint8_t>(frame.data(), frame.size()));
    }

    static bool send_pingresp(mqtt::TcpConnection& connection) {
        mqtt::WriteBuffer frame{};
        mqtt::encode_pingresp(frame);
        return connection.write(std::span<const std::uint8_t>(frame.data(), frame.size()));
    }

    static bool send_pubrel(mqtt::TcpConnection& connection,
                            const std::uint16_t packet_id) {
        mqtt::PubrelPacket pubrel_packet{};
        pubrel_packet.packet_id = packet_id;
        mqtt::WriteBuffer frame{};
        mqtt::encode_pubrel(frame, pubrel_packet);
        return connection.write(std::span<const std::uint8_t>(frame.data(), frame.size()));
    }

    static bool send_publish(mqtt::TcpConnection& connection,
                             const std::string& topic,
                             const std::string& payload,
                             const mqtt::QoS qos,
                             const std::optional<std::uint16_t> packet_id) {
        mqtt::PublishPacket publish_packet{};
        publish_packet.topic = mqtt::Utf8String{topic};
        publish_packet.payload = mqtt::BinaryData::from_string(payload);
        publish_packet.qos = qos;
        publish_packet.packet_id = packet_id;

        mqtt::WriteBuffer frame{};
        mqtt::encode_publish(frame, publish_packet);
        return connection.write(std::span<const std::uint8_t>(frame.data(), frame.size()));
    }

    static bool send_puback(mqtt::TcpConnection& connection,
                            const std::uint16_t packet_id) {
        mqtt::PubackPacket puback_packet{};
        puback_packet.packet_id = packet_id;
        puback_packet.reason_code = mqtt::ReasonCode::Success;
        mqtt::WriteBuffer frame{};
        mqtt::encode_puback(frame, puback_packet);
        return connection.write(std::span<const std::uint8_t>(frame.data(), frame.size()));
    }

    static bool send_pubrec(mqtt::TcpConnection& connection,
                            const std::uint16_t packet_id) {
        mqtt::PubrecPacket pubrec_packet{};
        pubrec_packet.packet_id = packet_id;
        pubrec_packet.reason_code = mqtt::ReasonCode::Success;
        mqtt::WriteBuffer frame{};
        mqtt::encode_pubrec(frame, pubrec_packet);
        return connection.write(std::span<const std::uint8_t>(frame.data(), frame.size()));
    }

    static bool send_pubcomp(mqtt::TcpConnection& connection,
                             const std::uint16_t packet_id) {
        mqtt::PubcompPacket pubcomp_packet{};
        pubcomp_packet.packet_id = packet_id;
        pubcomp_packet.reason_code = mqtt::ReasonCode::Success;
        mqtt::WriteBuffer frame{};
        mqtt::encode_pubcomp(frame, pubcomp_packet);
        return connection.write(std::span<const std::uint8_t>(frame.data(), frame.size()));
    }

    void handle_client(std::unique_ptr<mqtt::TcpConnection> connection) {
        if (connection == nullptr) {
            return;
        }

        mqtt::StreamBuffer stream_buffer{};
        bool active = true;

        while (active && running_.load()) {
            const std::optional<mqtt::AnyPacket> maybe_packet =
                read_next_packet(*connection, stream_buffer, 100U);
            if (!maybe_packet.has_value()) {
                continue;
            }

            if (std::holds_alternative<mqtt::ConnectPacket>(*maybe_packet)) {
                if (!send_connack(*connection)) {
                    break;
                }
                continue;
            }

            if (std::holds_alternative<mqtt::SubscribePacket>(*maybe_packet)) {
                const mqtt::SubscribePacket& subscribe_packet =
                    std::get<mqtt::SubscribePacket>(*maybe_packet);
                if (!send_suback(*connection, subscribe_packet.packet_id)) {
                    break;
                }

                if (!send_pingresp(*connection)) {
                    break;
                }
                if (!send_pubrel(*connection, 77U)) {
                    break;
                }
                if (!send_publish(*connection,
                                  "transport/number",
                                  "12.5",
                                  mqtt::QoS::AtLeastOnce,
                                  101U)) {
                    break;
                }
                if (!send_publish(*connection,
                                  "transport/text",
                                  "hello",
                                  mqtt::QoS::ExactlyOnce,
                                  102U)) {
                    break;
                }
                if (!send_publish(*connection,
                                  "transport/empty",
                                  "",
                                  mqtt::QoS::AtMostOnce,
                                  std::nullopt)) {
                    break;
                }
                continue;
            }

            if (std::holds_alternative<mqtt::UnsubscribePacket>(*maybe_packet)) {
                const mqtt::UnsubscribePacket& unsubscribe_packet =
                    std::get<mqtt::UnsubscribePacket>(*maybe_packet);
                if (!send_unsuback(*connection, unsubscribe_packet.packet_id)) {
                    break;
                }
                continue;
            }

            if (std::holds_alternative<mqtt::PublishPacket>(*maybe_packet)) {
                const mqtt::PublishPacket& publish_packet =
                    std::get<mqtt::PublishPacket>(*maybe_packet);
                if (publish_packet.qos == mqtt::QoS::AtLeastOnce &&
                    publish_packet.packet_id.has_value()) {
                    if (!send_puback(*connection, *publish_packet.packet_id)) {
                        break;
                    }
                }
                if (publish_packet.qos == mqtt::QoS::ExactlyOnce &&
                    publish_packet.packet_id.has_value()) {
                    if (!send_pubrec(*connection, *publish_packet.packet_id)) {
                        break;
                    }
                }
                continue;
            }

            if (std::holds_alternative<mqtt::PubrelPacket>(*maybe_packet)) {
                const mqtt::PubrelPacket& pubrel_packet =
                    std::get<mqtt::PubrelPacket>(*maybe_packet);
                if (!send_pubcomp(*connection, pubrel_packet.packet_id)) {
                    break;
                }
                continue;
            }

            if (std::holds_alternative<mqtt::PingreqPacket>(*maybe_packet)) {
                if (!send_pingresp(*connection)) {
                    break;
                }
                continue;
            }

            if (std::holds_alternative<mqtt::DisconnectPacket>(*maybe_packet)) {
                active = false;
                connection->close();
            }
        }
    }

    void accept_loop() {
        while (running_.load()) {
            try {
                if (!listener_.has_value()) {
                    return;
                }
                std::unique_ptr<mqtt::TcpConnection> connection = listener_->accept();
                std::lock_guard<std::mutex> lock{client_threads_mutex_};
                client_threads_.emplace_back(
                    [this, accepted_connection = std::move(connection)]() mutable {
                        handle_client(std::move(accepted_connection));
                    });
            } catch (...) {
                if (!running_.load()) {
                    return;
                }
            }
        }
    }

    std::atomic<bool> running_{false};
    std::optional<mqtt::TcpListener> listener_{};
    std::thread accept_thread_{};
    std::mutex client_threads_mutex_{};
    std::vector<std::thread> client_threads_{};
};

} // namespace

TEST_CASE("broker_transport_connect_poll_publish_and_unsubscribe_roundtrip",
          "[mqtt_client]") {
    FakeBrokerForTransportTest fake_broker{};
    fake_broker.start();

    yaha::YahaMqttClient::Transport transport = yaha::makeBrokerTransport();

    yaha::YahaMqttClient::Config config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = fake_broker.port();
    config.clientId = "transport-test-client";
    config.keepAliveInterval = std::chrono::seconds{5};

    REQUIRE(transport.connect(config));
    REQUIRE(transport.isConnected());

    transport.subscribe("transport/#", yaha::Qos::AtLeastOnce);

    std::vector<yaha::Message> received_messages{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{1000};
    while (received_messages.size() < 3U && std::chrono::steady_clock::now() < deadline) {
        const std::optional<yaha::Message> maybe_message = transport.pollIncoming();
        if (maybe_message.has_value()) {
            received_messages.push_back(*maybe_message);
        }
    }

    REQUIRE(received_messages.size() == 3U);
    CHECK(received_messages[0].topic() == "transport/number");
    REQUIRE(std::holds_alternative<double>(received_messages[0].value()));

    CHECK(received_messages[1].topic() == "transport/text");
    REQUIRE(std::holds_alternative<std::string>(received_messages[1].value()));
    CHECK(std::get<std::string>(received_messages[1].value()) == "hello");

    CHECK(received_messages[2].topic() == "transport/empty");
    REQUIRE(std::holds_alternative<std::string>(received_messages[2].value()));
    CHECK(std::get<std::string>(received_messages[2].value()).empty());

    transport.publish(yaha::Message{"out/qos0", std::string{"a"}, yaha::Qos::AtMostOnce, false});
    transport.publish(yaha::Message{"out/qos1", std::string{"b"}, yaha::Qos::AtLeastOnce, true});
    transport.publish(yaha::Message{"out/qos2", 42.0, yaha::Qos::ExactlyOnce, false});

    transport.ping();
    transport.unsubscribe("transport/#");
    transport.disconnect();

    CHECK_FALSE(transport.isConnected());

    fake_broker.stop();
}
