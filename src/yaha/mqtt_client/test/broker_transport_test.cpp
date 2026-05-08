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

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

constexpr std::size_t k_read_buffer_size{2048U};
constexpr std::uint16_t k_server_pubrel_packet_id{77U};
constexpr std::uint16_t k_server_number_packet_id{101U};
constexpr std::uint16_t k_server_text_packet_id{102U};
constexpr std::uint16_t k_server_forwarded_packet_id{103U};
constexpr int k_keep_alive_seconds{5};
constexpr double k_outgoing_qos2_value{42.0};
constexpr std::uint32_t k_fake_read_timeout_ms{100U};
constexpr int k_poll_deadline_ms{1000};
constexpr std::size_t k_expected_incoming_messages{8U};
constexpr double k_forwarded_numeric_value{77.5};

const std::string k_forwarded_inbound_payload =
    "{\"token\":\"abc\",\"message\":{\"topic\":\"transport/forwarded\",\"value\":\"sensor\",\"reason\":[{\"message\":\"src\",\"timestamp\":\"2026-05-08T10:00:00Z\"}]}}";
const std::string k_forwarded_outbound_payload =
    "{\"token\":\"forward\",\"message\":{\"topic\":\"out/raw\",\"value\":\"keep\",\"reason\":[{\"message\":\"why\",\"timestamp\":\"2026-05-08T10:00:00Z\"}]}}";
const std::string k_forwarded_numeric_reason_payload =
    "{\"message\":{\"topic\":\"transport/forwarded_numeric\",\"value\":77.5,\"reason\":\"manual\"}}";
const std::string k_forwarded_bool_payload =
    "{\"message\":{\"topic\":\"transport/forwarded_bool\",\"value\":true}}";
const std::string k_forwarded_escaped_payload =
    "{\"message\":{\"topic\":\"transport\\/forwarded_escaped\",\"value\":\"line\\nvalue\",\"reason\":[{\"message\":\"plain\"}]}}";
const std::string k_forwarded_invalid_value_payload =
    "{\"message\":{\"topic\":\"transport/forwarded_invalid\",\"value\":{}}}";

class FakeBrokerForTransportTest {
public:
    struct PublishedRecord {
        std::string topic{};
        std::string payload{};
    };

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

    [[nodiscard]] std::vector<PublishedRecord> publishedRecords() const {
        std::lock_guard<std::mutex> lock{published_records_mutex_};
        return published_records_;
    }

private:
    static std::optional<mqtt::AnyPacket> read_next_packet(
        mqtt::TcpConnection& connection,
        mqtt::StreamBuffer& stream_buffer,
        const std::uint32_t timeout_ms) {
        std::array<std::uint8_t, k_read_buffer_size> read_buffer{};

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

    [[nodiscard]] static bool handle_subscribe_packet(mqtt::TcpConnection& connection,
                                                      const mqtt::SubscribePacket& subscribe_packet) {
        if (!send_suback(connection, subscribe_packet.packet_id)) {
            return false;
        }

        if (!send_pingresp(connection)) {
            return false;
        }
        if (!send_pubrel(connection, k_server_pubrel_packet_id)) {
            return false;
        }
        if (!send_publish(connection,
                          "transport/number",
                          "12.5",
                          mqtt::QoS::AtLeastOnce,
                          k_server_number_packet_id)) {
            return false;
        }
        if (!send_publish(connection,
                          "transport/text",
                          "hello",
                          mqtt::QoS::ExactlyOnce,
                          k_server_text_packet_id)) {
            return false;
        }
        if (!send_publish(connection,
                          "transport/empty",
                          "",
                          mqtt::QoS::AtMostOnce,
                          std::nullopt)) {
            return false;
        }
        if (!send_publish(connection,
                          "transport/forwarded",
                          k_forwarded_inbound_payload,
                          mqtt::QoS::AtLeastOnce,
                          k_server_forwarded_packet_id)) {
            return false;
        }
        if (!send_publish(connection,
                          "transport/forwarded_numeric",
                          k_forwarded_numeric_reason_payload,
                          mqtt::QoS::AtMostOnce,
                          std::nullopt)) {
            return false;
        }
        if (!send_publish(connection,
                          "transport/forwarded_bool",
                          k_forwarded_bool_payload,
                          mqtt::QoS::AtMostOnce,
                          std::nullopt)) {
            return false;
        }
        if (!send_publish(connection,
                          "transport/forwarded_escaped",
                          k_forwarded_escaped_payload,
                          mqtt::QoS::AtMostOnce,
                          std::nullopt)) {
            return false;
        }
        if (!send_publish(connection,
                          "transport/forwarded_invalid",
                          k_forwarded_invalid_value_payload,
                          mqtt::QoS::AtMostOnce,
                          std::nullopt)) {
            return false;
        }

        return true;
    }

    void store_published_record(const mqtt::PublishPacket& publish_packet) {
        PublishedRecord publishedRecord{};
        publishedRecord.topic = publish_packet.topic.value;
        publishedRecord.payload.assign(publish_packet.payload.data.begin(),
                                       publish_packet.payload.data.end());
        std::lock_guard<std::mutex> lock{published_records_mutex_};
        published_records_.push_back(std::move(publishedRecord));
    }

    [[nodiscard]] static bool acknowledge_publish_if_needed(mqtt::TcpConnection& connection,
                                                             const mqtt::PublishPacket& publish_packet) {
        if (!publish_packet.packet_id.has_value()) {
            return true;
        }

        if (publish_packet.qos == mqtt::QoS::AtLeastOnce) {
            return send_puback(connection, *publish_packet.packet_id);
        }
        if (publish_packet.qos == mqtt::QoS::ExactlyOnce) {
            return send_pubrec(connection, *publish_packet.packet_id);
        }

        return true;
    }

    bool handle_packet(mqtt::TcpConnection& connection,
                       const mqtt::AnyPacket& packet,
                       bool& active) {
        return std::visit(
            [this, &connection, &active](const auto& concretePacket) -> bool {
                using PacketType = std::decay_t<decltype(concretePacket)>;

                if constexpr (std::is_same_v<PacketType, mqtt::ConnectPacket>) {
                    return send_connack(connection);
                }
                if constexpr (std::is_same_v<PacketType, mqtt::SubscribePacket>) {
                    return handle_subscribe_packet(connection, concretePacket);
                }
                if constexpr (std::is_same_v<PacketType, mqtt::UnsubscribePacket>) {
                    return send_unsuback(connection, concretePacket.packet_id);
                }
                if constexpr (std::is_same_v<PacketType, mqtt::PublishPacket>) {
                    store_published_record(concretePacket);
                    return acknowledge_publish_if_needed(connection, concretePacket);
                }
                if constexpr (std::is_same_v<PacketType, mqtt::PubrelPacket>) {
                    return send_pubcomp(connection, concretePacket.packet_id);
                }
                if constexpr (std::is_same_v<PacketType, mqtt::PingreqPacket>) {
                    return send_pingresp(connection);
                }
                if constexpr (std::is_same_v<PacketType, mqtt::DisconnectPacket>) {
                    active = false;
                    connection.close();
                    return true;
                }
                return true;
            },
            packet);
    }

    void handle_client(std::unique_ptr<mqtt::TcpConnection> connection) {
        if (connection == nullptr) {
            return;
        }

        mqtt::StreamBuffer stream_buffer{};
        bool active = true;

        while (active && running_.load()) {
            const std::optional<mqtt::AnyPacket> maybe_packet =
                read_next_packet(*connection, stream_buffer, k_fake_read_timeout_ms);
            if (!maybe_packet.has_value()) {
                continue;
            }

            if (!handle_packet(*connection, *maybe_packet, active)) {
                break;
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
    mutable std::mutex published_records_mutex_{};
    std::vector<PublishedRecord> published_records_{};
};

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("broker_transport_connect_poll_publish_and_unsubscribe_roundtrip",
          "[mqtt_client]") {
    FakeBrokerForTransportTest fake_broker{};
    fake_broker.start();

    yaha::YahaMqttClient::Transport transport = yaha::makeBrokerTransport();

    yaha::YahaMqttClient::Config config{};
    config.brokerHost = "127.0.0.1";
    config.brokerPort = fake_broker.port();
    config.clientId = "transport-test-client";
    config.keepAliveInterval = std::chrono::seconds{k_keep_alive_seconds};

    REQUIRE(transport.connect(config));
    REQUIRE(transport.isConnected());

    transport.subscribe("transport/#", yaha::Qos::AtLeastOnce);

    std::vector<yaha::Message> received_messages{};
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds{k_poll_deadline_ms};
        while (received_messages.size() < k_expected_incoming_messages &&
            std::chrono::steady_clock::now() < deadline) {
        const std::optional<yaha::Message> maybe_message = transport.pollIncoming();
        if (maybe_message.has_value()) {
            received_messages.push_back(*maybe_message);
        }
    }

    REQUIRE(received_messages.size() == k_expected_incoming_messages);
    CHECK(received_messages[0].topic() == "transport/number");
    REQUIRE(std::holds_alternative<double>(received_messages[0].value()));

    CHECK(received_messages[1].topic() == "transport/text");
    REQUIRE(std::holds_alternative<std::string>(received_messages[1].value()));
    CHECK(std::get<std::string>(received_messages[1].value()) == "hello");

    CHECK(received_messages[2].topic() == "transport/empty");
    REQUIRE(std::holds_alternative<std::string>(received_messages[2].value()));
    CHECK(std::get<std::string>(received_messages[2].value()).empty());

    CHECK(received_messages[3].topic() == "transport/forwarded");
    REQUIRE(std::holds_alternative<std::string>(received_messages[3].value()));
    CHECK(std::get<std::string>(received_messages[3].value()) == "sensor");
    REQUIRE(received_messages[3].reason().size() == 1U);
    CHECK(received_messages[3].reason().front().message == "src");
    REQUIRE(received_messages[3].rawPayload().has_value());
    CHECK(*received_messages[3].rawPayload() == k_forwarded_inbound_payload);

    CHECK(received_messages[4].topic() == "transport/forwarded_numeric");
    REQUIRE(std::holds_alternative<double>(received_messages[4].value()));
    CHECK(std::get<double>(received_messages[4].value()) == k_forwarded_numeric_value);
    REQUIRE(received_messages[4].reason().size() == 1U);
    CHECK(received_messages[4].reason().front().message == "manual");
    REQUIRE(received_messages[4].rawPayload().has_value());
    CHECK(*received_messages[4].rawPayload() == k_forwarded_numeric_reason_payload);

    CHECK(received_messages[5].topic() == "transport/forwarded_bool");
    REQUIRE(std::holds_alternative<std::string>(received_messages[5].value()));
    CHECK(std::get<std::string>(received_messages[5].value()) == "true");
    CHECK(received_messages[5].reason().empty());
    REQUIRE(received_messages[5].rawPayload().has_value());
    CHECK(*received_messages[5].rawPayload() == k_forwarded_bool_payload);

    CHECK(received_messages[6].topic() == "transport/forwarded_escaped");
    REQUIRE(std::holds_alternative<std::string>(received_messages[6].value()));
    CHECK(std::get<std::string>(received_messages[6].value()) == "linenvalue");
    REQUIRE(received_messages[6].reason().size() == 1U);
    CHECK(received_messages[6].reason().front().message == "plain");
    REQUIRE(received_messages[6].rawPayload().has_value());
    CHECK(*received_messages[6].rawPayload() == k_forwarded_escaped_payload);

    CHECK(received_messages[7].topic() == "transport/forwarded_invalid");
    REQUIRE(std::holds_alternative<std::string>(received_messages[7].value()));
    CHECK(std::get<std::string>(received_messages[7].value()) == k_forwarded_invalid_value_payload);
    CHECK(received_messages[7].reason().empty());
    REQUIRE(received_messages[7].rawPayload().has_value());
    CHECK(*received_messages[7].rawPayload() == k_forwarded_invalid_value_payload);

    transport.publish(yaha::Message{"out/qos0", std::string{"a"}, yaha::Qos::AtMostOnce, false});
    transport.publish(yaha::Message{"out/qos1", std::string{"b"}, yaha::Qos::AtLeastOnce, true});
    transport.publish(yaha::Message{"out/qos2", k_outgoing_qos2_value, yaha::Qos::ExactlyOnce, false});
    yaha::Message rawPublish{"out/raw", std::string{"fallback"}, yaha::Qos::AtLeastOnce, false};
    rawPublish.setRawPayload(k_forwarded_outbound_payload);
    transport.publish(rawPublish);

    transport.ping();
    transport.unsubscribe("transport/#");
    transport.disconnect();

    const auto publishedRecords = fake_broker.publishedRecords();
    REQUIRE(publishedRecords.size() >= 4U);
    const auto rawRecord = std::find_if(publishedRecords.begin(), publishedRecords.end(),
                                        [](const auto& item) {
        return item.topic == "out/raw";
    });
    REQUIRE(rawRecord != publishedRecords.end());
    CHECK(rawRecord->payload == k_forwarded_outbound_payload);

    CHECK_FALSE(transport.isConnected());

    fake_broker.stop();
}
