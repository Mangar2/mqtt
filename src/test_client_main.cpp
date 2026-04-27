#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "client/connection_negotiator.h"
#include "client_api/client_api_error.h"
#include "client_api/client_config.h"
#include "codec/packet/connect_codec.h"
#include "codec/packet/control_codec.h"
#include "codec/packet/publish_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "data_model/property/property_id.h"
#include "network/stream_buffer.h"
#include "network/tcp_connection.h"
#include "test_client/test_client_cli.h"
#include "test_client/test_client_profile.h"
#include "transport/websocket_frame_codec.h"

namespace mqtt {
namespace {

std::atomic<bool> g_stop_requested{false};

void on_signal_received(int signal_number) {
  if (signal_number == SIGINT || signal_number == SIGTERM) {
    g_stop_requested.store(true);
  }
}

void install_signal_handlers() {
  std::signal(SIGINT, on_signal_received);
  std::signal(SIGTERM, on_signal_received);
}

[[nodiscard]] TestClientProfile build_effective_profile(
    const TestClientCliOptions &cli_options) {
  TestClientProfile profile;
  if (!cli_options.profile_path.empty()) {
    profile = load_test_client_profile_from_file(cli_options.profile_path);
  }

  for (const auto &override_pair : cli_options.overrides) {
    apply_profile_override(profile, override_pair.first, override_pair.second);
  }

  validate_test_client_profile_or_throw(profile);
  return profile;
}

[[nodiscard]] QoS qos_from_u8_or_throw(const uint8_t qos_value,
                                       const std::string_view field_name) {
  if (qos_value == 0U) {
    return QoS::AtMostOnce;
  }
  if (qos_value == 1U) {
    return QoS::AtLeastOnce;
  }
  if (qos_value == 2U) {
    return QoS::ExactlyOnce;
  }
  throw std::invalid_argument("Invalid QoS in " + std::string(field_name));
}

ClientConfig make_client_config_from_profile(const TestClientProfile &profile) {
  ClientConfig config;
  config.broker_host = profile.host;
  config.broker_port = profile.port;
  config.transport = profile.transport == TestClientTransport::Mqtt
                         ? ClientTransportType::Tcp
                         : ClientTransportType::WebSocket;

  config.client_id = profile.client_id;
  config.clean_start = profile.clean_start;
  config.keep_alive_seconds = profile.keep_alive_seconds;
  config.session_expiry_interval_seconds =
      profile.session_expiry_interval_seconds;
  config.receive_maximum = profile.receive_maximum;
  config.topic_alias_maximum = profile.topic_alias_maximum;

  config.reconnect_backoff.initial_delay =
      std::chrono::milliseconds(profile.reconnect_period_ms);
  config.reconnect_backoff.max_delay =
      std::chrono::milliseconds(profile.reconnect_period_ms);
  config.reconnect_backoff.multiplier = 1.0;

  if (profile.username.has_value()) {
    config.credentials.username = profile.username;
  }
  if (profile.password.has_value()) {
    config.credentials.password = profile.password;
  }

  return config;
}

void append_connect_properties_from_profile(ConnectPacket &connect_packet,
                                            const TestClientProfile &profile) {
  if (profile.maximum_packet_size > 0U) {
    connect_packet.properties.push_back(
        Property{.id = PropertyId::MaximumPacketSize,
                 .value = FourByteInteger{profile.maximum_packet_size}});
  }

  connect_packet.properties.push_back(
      Property{.id = PropertyId::RequestResponseInformation,
               .value = static_cast<uint8_t>(
                   profile.request_response_information ? 1U : 0U)});
  connect_packet.properties.push_back(
      Property{.id = PropertyId::RequestProblemInformation,
               .value = static_cast<uint8_t>(
                   profile.request_problem_information ? 1U : 0U)});

  for (const auto &entry : profile.connect_user_properties) {
    connect_packet.properties.push_back(
        Property{.id = PropertyId::UserProperty,
                 .value = Utf8StringPair{.name = Utf8String{entry.first},
                                         .value = Utf8String{entry.second}}});
  }

  if (profile.authentication_method.has_value()) {
    connect_packet.properties.push_back(
        Property{.id = PropertyId::AuthenticationMethod,
                 .value = Utf8String{*profile.authentication_method}});
  }
  if (profile.authentication_data.has_value()) {
    connect_packet.properties.push_back(
        Property{.id = PropertyId::AuthenticationData,
                 .value =
                     BinaryData::from_string(*profile.authentication_data)});
  }
}

void append_will_from_profile(ConnectPacket &connect_packet,
                              const TestClientProfile &profile) {
  if (!profile.will_topic.has_value()) {
    return;
  }

  WillData will_data;
  will_data.topic = Utf8String{*profile.will_topic};
  will_data.payload = BinaryData::from_string(profile.will_payload);
  will_data.qos = qos_from_u8_or_throw(profile.will_qos, "will_qos");
  will_data.retain = profile.will_retain;

  if (profile.will_delay_interval_seconds > 0U) {
    will_data.properties.push_back(
        Property{.id = PropertyId::WillDelayInterval,
                 .value = FourByteInteger{profile.will_delay_interval_seconds}});
  }
  if (profile.will_payload_format_indicator.has_value()) {
    will_data.properties.push_back(
        Property{.id = PropertyId::PayloadFormatIndicator,
                 .value = *profile.will_payload_format_indicator});
  }
  if (profile.will_message_expiry_interval_seconds.has_value()) {
    will_data.properties.push_back(
        Property{.id = PropertyId::MessageExpiryInterval,
                 .value = FourByteInteger{
                     *profile.will_message_expiry_interval_seconds}});
  }
  if (profile.will_content_type.has_value()) {
    will_data.properties.push_back(
        Property{.id = PropertyId::ContentType,
                 .value = Utf8String{*profile.will_content_type}});
  }
  if (profile.will_response_topic.has_value()) {
    will_data.properties.push_back(
        Property{.id = PropertyId::ResponseTopic,
                 .value = Utf8String{*profile.will_response_topic}});
  }
  if (profile.will_correlation_data.has_value()) {
    will_data.properties.push_back(
        Property{.id = PropertyId::CorrelationData,
                 .value = BinaryData::from_string(*profile.will_correlation_data)});
  }
  for (const auto &entry : profile.will_user_properties) {
    will_data.properties.push_back(
        Property{.id = PropertyId::UserProperty,
                 .value = Utf8StringPair{.name = Utf8String{entry.first},
                                         .value = Utf8String{entry.second}}});
  }

  connect_packet.will = std::move(will_data);
}

[[nodiscard]] ConnectPacket
build_connect_packet_from_profile(const TestClientProfile &profile) {
  const ClientConfig client_config = make_client_config_from_profile(profile);
  validate_client_config_or_throw(client_config);

  ConnectPacket connect_packet = build_connect_packet(client_config);
  append_connect_properties_from_profile(connect_packet, profile);
  append_will_from_profile(connect_packet, profile);
  return connect_packet;
}

[[nodiscard]] uint8_t hex_nibble_or_throw(char character) {
  if (character >= '0' && character <= '9') {
    return static_cast<uint8_t>(character - '0');
  }
  if (character >= 'a' && character <= 'f') {
    return static_cast<uint8_t>(10 + (character - 'a'));
  }
  if (character >= 'A' && character <= 'F') {
    return static_cast<uint8_t>(10 + (character - 'A'));
  }
  throw std::invalid_argument("Invalid hex character");
}

[[nodiscard]] std::vector<uint8_t>
decode_hex_to_bytes_or_throw(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (const char character : text) {
    if (!std::isspace(static_cast<unsigned char>(character))) {
      normalized.push_back(character);
    }
  }

  if ((normalized.size() % 2U) != 0U) {
    throw std::invalid_argument("Hex payload requires even number of digits");
  }

  std::vector<uint8_t> bytes;
  bytes.reserve(normalized.size() / 2U);
  for (std::size_t index = 0U; index < normalized.size(); index += 2U) {
    const uint8_t high = hex_nibble_or_throw(normalized[index]);
    const uint8_t low = hex_nibble_or_throw(normalized[index + 1U]);
    bytes.push_back(static_cast<uint8_t>((high << 4U) | low));
  }
  return bytes;
}

[[nodiscard]] std::vector<uint8_t>
decode_base64_to_bytes_or_throw(std::string_view text) {
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::vector<uint8_t> decoded;
  uint32_t accumulator = 0U;
  uint8_t bit_count = 0U;

  for (const char character : text) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      continue;
    }
    if (character == '=') {
      break;
    }

    const std::size_t value_index = alphabet.find(character);
    if (value_index == std::string_view::npos) {
      throw std::invalid_argument("Invalid base64 character");
    }

    accumulator = (accumulator << 6U) | static_cast<uint32_t>(value_index);
    bit_count = static_cast<uint8_t>(bit_count + 6U);

    while (bit_count >= 8U) {
      bit_count = static_cast<uint8_t>(bit_count - 8U);
      decoded.push_back(
          static_cast<uint8_t>((accumulator >> bit_count) & 0xFFU));
    }
  }

  return decoded;
}

[[nodiscard]] BinaryData binary_from_text_with_encoding_or_throw(
    const std::string &text, const std::string &encoding,
    const std::string_view field_name) {
  BinaryData binary_data;

  if (encoding == "raw" || encoding == "json") {
    binary_data = BinaryData::from_string(text);
  } else if (encoding == "hex") {
    binary_data.data = decode_hex_to_bytes_or_throw(text);
  } else if (encoding == "base64") {
    binary_data.data = decode_base64_to_bytes_or_throw(text);
  } else if (encoding == "binary" || encoding == "protobuf" ||
             encoding == "avro") {
    binary_data = BinaryData::from_string(text);
  } else {
    throw std::invalid_argument("Unsupported encoding in " +
                                std::string(field_name));
  }

  return binary_data;
}

[[nodiscard]] BinaryData read_file_as_binary_or_throw(const std::string &path) {
  std::ifstream file_stream(path, std::ios::binary);
  if (!file_stream.is_open()) {
    throw std::runtime_error("Failed to open payload file: " + path);
  }

  BinaryData binary_data;
  file_stream.seekg(0, std::ios::end);
  const std::streamoff file_size = file_stream.tellg();
  file_stream.seekg(0, std::ios::beg);
  if (file_size > 0) {
    binary_data.data.resize(static_cast<std::size_t>(file_size));
    file_stream.read(reinterpret_cast<char *>(binary_data.data.data()),
                     static_cast<std::streamsize>(binary_data.data.size()));
  }

  if (!file_stream.good() && !file_stream.eof()) {
    throw std::runtime_error("Failed to read payload file: " + path);
  }

  return binary_data;
}

[[nodiscard]] std::string read_stdin_payload(const bool multiline_mode) {
  std::string payload_text;
  if (!multiline_mode) {
    std::getline(std::cin, payload_text);
    return payload_text;
  }

  std::string line;
  bool first_line = true;
  while (std::getline(std::cin, line)) {
    if (!first_line) {
      payload_text += '\n';
    }
    first_line = false;
    payload_text += line;
  }
  return payload_text;
}

[[nodiscard]] BinaryData
resolve_publish_payload_or_throw(const TestClientProfile &profile) {
  if (profile.publish_payload.has_value()) {
    return binary_from_text_with_encoding_or_throw(
        *profile.publish_payload, profile.publish_payload_encoding,
        "publish_payload");
  }

  if (profile.publish_payload_stdin || profile.publish_payload_stdin_multiline) {
    const std::string stdin_payload =
        read_stdin_payload(profile.publish_payload_stdin_multiline);
    return binary_from_text_with_encoding_or_throw(
        stdin_payload, profile.publish_payload_encoding, "stdin payload");
  }

  if (profile.publish_payload_file.has_value()) {
    if (profile.publish_payload_encoding == "binary" ||
        profile.publish_payload_encoding == "protobuf" ||
        profile.publish_payload_encoding == "avro") {
      return read_file_as_binary_or_throw(*profile.publish_payload_file);
    }

    std::ifstream text_file(*profile.publish_payload_file);
    if (!text_file.is_open()) {
      throw std::runtime_error("Failed to open payload file: " +
                               *profile.publish_payload_file);
    }
    std::string file_text((std::istreambuf_iterator<char>(text_file)),
                          std::istreambuf_iterator<char>());
    return binary_from_text_with_encoding_or_throw(
        file_text, profile.publish_payload_encoding, "publish_payload_file");
  }

  return BinaryData{};
}

[[nodiscard]] PublishPacket
build_publish_packet_from_profile_or_throw(const TestClientProfile &profile) {
  if (!profile.publish_topic.has_value()) {
    throw std::invalid_argument("publish command requires --topic");
  }

  PublishPacket publish_packet;
  publish_packet.topic = Utf8String{*profile.publish_topic};
  publish_packet.qos = qos_from_u8_or_throw(profile.publish_qos, "publish_qos");
  publish_packet.retain = profile.publish_retain;
  publish_packet.dup = profile.publish_dup;
  publish_packet.payload = resolve_publish_payload_or_throw(profile);

  if (publish_packet.qos != QoS::AtMostOnce) {
    publish_packet.packet_id = 1U;
  }

  if (profile.publish_payload_format_indicator.has_value()) {
    publish_packet.properties.push_back(
        Property{.id = PropertyId::PayloadFormatIndicator,
                 .value = *profile.publish_payload_format_indicator});
  }
  if (profile.publish_message_expiry_interval_seconds.has_value()) {
    publish_packet.properties.push_back(
        Property{.id = PropertyId::MessageExpiryInterval,
                 .value = FourByteInteger{
                     *profile.publish_message_expiry_interval_seconds}});
  }
  if (profile.publish_topic_alias.has_value()) {
    publish_packet.properties.push_back(
        Property{.id = PropertyId::TopicAlias,
                 .value = TwoByteInteger{*profile.publish_topic_alias}});
  }
  if (profile.publish_response_topic.has_value()) {
    publish_packet.properties.push_back(
        Property{.id = PropertyId::ResponseTopic,
                 .value = Utf8String{*profile.publish_response_topic}});
  }
  if (profile.publish_correlation_data.has_value()) {
    publish_packet.properties.push_back(
        Property{.id = PropertyId::CorrelationData,
                 .value = binary_from_text_with_encoding_or_throw(
                     *profile.publish_correlation_data,
                     profile.publish_correlation_data_encoding,
                     "publish_correlation_data")});
  }
  if (profile.publish_subscription_identifier.has_value()) {
    publish_packet.properties.push_back(
        Property{.id = PropertyId::SubscriptionIdentifier,
                 .value = VariableByteInteger{
                     *profile.publish_subscription_identifier}});
  }
  if (profile.publish_content_type.has_value()) {
    publish_packet.properties.push_back(
        Property{.id = PropertyId::ContentType,
                 .value = Utf8String{*profile.publish_content_type}});
  }
  for (const auto &entry : profile.publish_user_properties) {
    publish_packet.properties.push_back(
        Property{.id = PropertyId::UserProperty,
                 .value = Utf8StringPair{.name = Utf8String{entry.first},
                                         .value = Utf8String{entry.second}}});
  }

  return publish_packet;
}

[[nodiscard]] std::vector<uint8_t>
encode_masked_frame(const WsOpcode opcode, std::span<const uint8_t> payload) {
  constexpr std::array<uint8_t, 4U> mask_key{0x12U, 0x34U, 0x56U, 0x78U};

  std::vector<uint8_t> frame;
  frame.push_back(static_cast<uint8_t>(0x80U | static_cast<uint8_t>(opcode)));

  const std::size_t payload_size = payload.size();
  if (payload_size <= 125U) {
    frame.push_back(static_cast<uint8_t>(0x80U | payload_size));
  } else if (payload_size <= 65535U) {
    frame.push_back(0xFEU);
    frame.push_back(static_cast<uint8_t>((payload_size >> 8U) & 0xFFU));
    frame.push_back(static_cast<uint8_t>(payload_size & 0xFFU));
  } else {
    frame.push_back(0xFFU);
    for (int shift_value = 56; shift_value >= 0; shift_value -= 8) {
      frame.push_back(static_cast<uint8_t>((payload_size >> shift_value) &
                                           0xFFU));
    }
  }

  frame.insert(frame.end(), mask_key.begin(), mask_key.end());
  for (std::size_t index = 0U; index < payload_size; ++index) {
    frame.push_back(payload[index] ^ mask_key[index % 4U]);
  }

  return frame;
}

[[nodiscard]] std::vector<uint8_t>
encode_masked_binary_frame(std::span<const uint8_t> payload) {
  return encode_masked_frame(WsOpcode::Binary, payload);
}

void write_transport_packet_or_throw(TcpConnection &connection,
                                     const TestClientTransport transport,
                                     std::span<const uint8_t> packet_bytes) {
  bool write_ok = false;
  if (transport == TestClientTransport::Mqtt) {
    write_ok = connection.write(packet_bytes);
  } else {
    const std::vector<uint8_t> ws_frame =
        encode_masked_binary_frame(packet_bytes);
    write_ok = connection.write(ws_frame);
  }

  if (!write_ok) {
    throw ClientException(ClientError::WriteFailed,
                          "Failed to write MQTT packet");
  }
}

[[nodiscard]] std::string build_ws_upgrade_request(
    const TestClientProfile &profile) {
  std::string request = "GET " + profile.ws_path + " HTTP/1.1\r\n";
  request += "Host: " + profile.host + ":" + std::to_string(profile.port) +
             "\r\n";
  request += "Upgrade: websocket\r\n";
  request += "Connection: Upgrade\r\n";
  request += "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
  request += "Sec-WebSocket-Version: 13\r\n";
  request += "Sec-WebSocket-Protocol: mqtt\r\n";

  for (const std::string &header_line : profile.ws_headers) {
    request += header_line;
    request += "\r\n";
  }

  request += "\r\n";
  return request;
}

void perform_ws_upgrade(TcpConnection &connection,
                        const TestClientProfile &profile) {
  const std::string request = build_ws_upgrade_request(profile);
  const std::span<const uint8_t> request_bytes(
      reinterpret_cast<const uint8_t *>(request.data()), request.size());

  if (!connection.write(request_bytes)) {
    throw ClientException(ClientError::WriteFailed,
                          "Failed to send WebSocket upgrade request");
  }

  connection.set_receive_timeout(5000U);
  std::array<uint8_t, 4096U> receive_buffer{};
  std::string response;

  while (response.find("\r\n\r\n") == std::string::npos) {
    const std::ptrdiff_t bytes_read = connection.read(receive_buffer);
    if (bytes_read <= 0) {
      if (bytes_read < 0 && connection.last_read_timed_out()) {
        throw ClientException(ClientError::Timeout,
                              "Timed out waiting for WebSocket upgrade response");
      }
      throw ClientException(ClientError::ReadFailed,
                            "Socket closed during WebSocket upgrade");
    }

    response.append(reinterpret_cast<const char *>(receive_buffer.data()),
                    static_cast<std::size_t>(bytes_read));
    if (response.size() > 16384U) {
      throw ClientException(ClientError::ProtocolError,
                            "WebSocket upgrade response is too large");
    }
  }

  if (response.rfind("HTTP/1.1 101", 0U) != 0U) {
    throw ClientException(ClientError::ProtocolError,
                          "WebSocket upgrade failed with non-101 status");
  }
}

void negotiate_mqtt_connect_over_ws(TcpConnection &connection,
                                    const ConnectPacket &connect_packet) {
  WriteBuffer connect_bytes;
  encode_connect(connect_bytes, connect_packet);
  write_transport_packet_or_throw(connection, TestClientTransport::Ws,
                                  connect_bytes);

  connection.set_receive_timeout(5000U);
  WebSocketFrameCodec ws_frame_codec;
  StreamBuffer stream_buffer;
  std::array<uint8_t, 4096U> receive_buffer{};

  while (true) {
    const std::ptrdiff_t bytes_read = connection.read(receive_buffer);
    if (bytes_read <= 0) {
      if (bytes_read < 0 && connection.last_read_timed_out()) {
        throw ClientException(ClientError::Timeout,
                              "Timed out waiting for CONNACK over WebSocket");
      }
      throw ClientException(ClientError::ReadFailed,
                            "Socket closed before CONNACK over WebSocket");
    }

    ws_frame_codec.append(std::span<const uint8_t>(
        receive_buffer.data(), static_cast<std::size_t>(bytes_read)));

    while (ws_frame_codec.has_frame()) {
      const WsFrame frame = ws_frame_codec.consume_frame();
      if (frame.opcode == WsOpcode::Ping) {
        const std::vector<uint8_t> pong_frame =
            encode_masked_frame(WsOpcode::Pong, frame.payload);
        (void)connection.write(pong_frame);
        continue;
      }
      if (frame.opcode != WsOpcode::Binary) {
        continue;
      }

      (void)stream_buffer.append(frame.payload);
      while (stream_buffer.has_complete_packet()) {
        const std::vector<uint8_t> packet_bytes = stream_buffer.consume_packet();
        ReadBuffer packet_buffer(
            std::span<const uint8_t>(packet_bytes.data(), packet_bytes.size()));
        const AnyPacket any_packet = read_packet(packet_buffer);
        if (!std::holds_alternative<ConnackPacket>(any_packet)) {
          throw ClientException(ClientError::ProtocolError,
                                "First MQTT packet over WebSocket is not CONNACK");
        }

        const ConnackPacket &connack_packet =
            std::get<ConnackPacket>(any_packet);
        if (is_error(connack_packet.reason_code)) {
          throw ClientException(ClientError::NegotiationRejected,
                                "Broker rejected CONNECT over WebSocket",
                                connack_packet.reason_code);
        }
        return;
      }
    }
  }
}

[[nodiscard]] AnyPacket read_next_transport_packet_or_throw(
    TcpConnection &connection, const TestClientTransport transport,
    StreamBuffer &mqtt_stream_buffer, WebSocketFrameCodec &ws_frame_codec,
    const uint32_t timeout_ms) {
  connection.set_receive_timeout(timeout_ms);
  std::array<uint8_t, 4096U> receive_buffer{};

  while (true) {
    if (transport == TestClientTransport::Mqtt &&
        mqtt_stream_buffer.has_complete_packet()) {
      const std::vector<uint8_t> packet_bytes =
          mqtt_stream_buffer.consume_packet();
      ReadBuffer packet_buffer(
          std::span<const uint8_t>(packet_bytes.data(), packet_bytes.size()));
      return read_packet(packet_buffer);
    }

    const std::ptrdiff_t bytes_read = connection.read(receive_buffer);
    if (bytes_read <= 0) {
      if (bytes_read < 0 && connection.last_read_timed_out()) {
        throw ClientException(ClientError::Timeout,
                              "Timed out waiting for broker packet");
      }
      throw ClientException(ClientError::ReadFailed,
                            "Socket closed while waiting for broker packet");
    }

    if (transport == TestClientTransport::Mqtt) {
      (void)mqtt_stream_buffer.append(std::span<const uint8_t>(
          receive_buffer.data(), static_cast<std::size_t>(bytes_read)));
      continue;
    }

    ws_frame_codec.append(std::span<const uint8_t>(
        receive_buffer.data(), static_cast<std::size_t>(bytes_read)));
    while (ws_frame_codec.has_frame()) {
      const WsFrame frame = ws_frame_codec.consume_frame();
      if (frame.opcode == WsOpcode::Ping) {
        const std::vector<uint8_t> pong_frame =
            encode_masked_frame(WsOpcode::Pong, frame.payload);
        (void)connection.write(pong_frame);
        continue;
      }
      if (frame.opcode == WsOpcode::Close) {
        throw ClientException(ClientError::ProtocolError,
                              "WebSocket close frame received");
      }
      if (frame.opcode != WsOpcode::Binary) {
        continue;
      }

      (void)mqtt_stream_buffer.append(frame.payload);
      if (mqtt_stream_buffer.has_complete_packet()) {
        const std::vector<uint8_t> packet_bytes =
            mqtt_stream_buffer.consume_packet();
        ReadBuffer packet_buffer(std::span<const uint8_t>(packet_bytes.data(),
                                                          packet_bytes.size()));
        return read_packet(packet_buffer);
      }
    }
  }
}

void keep_connection_open(TcpConnection &connection,
                          const TestClientTransport transport,
                          const uint16_t keep_alive_seconds) {
  const uint16_t ping_interval_seconds =
      keep_alive_seconds > 2U ? static_cast<uint16_t>(keep_alive_seconds / 2U)
                              : 1U;

  auto next_ping = std::chrono::steady_clock::now() +
                   std::chrono::seconds(ping_interval_seconds);

  while (!g_stop_requested.load()) {
    if (std::chrono::steady_clock::now() >= next_ping) {
      WriteBuffer ping_request;
      encode_pingreq(ping_request);
      write_transport_packet_or_throw(connection, transport, ping_request);
      next_ping = std::chrono::steady_clock::now() +
                  std::chrono::seconds(ping_interval_seconds);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

void send_disconnect_best_effort(TcpConnection &connection,
                                 const TestClientTransport transport) {
  WriteBuffer disconnect_bytes;
  DisconnectPacket disconnect_packet;
  disconnect_packet.reason_code = ReasonCode::Success;
  encode_disconnect(disconnect_bytes, disconnect_packet);
  try {
    write_transport_packet_or_throw(connection, transport, disconnect_bytes);
  } catch (...) {
  }
}

int run_connect_session(const TestClientProfile &profile) {
  const ConnectPacket connect_packet = build_connect_packet_from_profile(profile);
  TcpConnection connection =
      ConnectionNegotiator::dial_tcp(profile.host, profile.port);

  if (profile.transport == TestClientTransport::Mqtt) {
    (void)ConnectionNegotiator::negotiate(connection, connect_packet, 5000U);
    std::cout << "Connected over mqtt to " << profile.host << ':' << profile.port
              << " as " << profile.client_id << '\n';
  } else {
    perform_ws_upgrade(connection, profile);
    negotiate_mqtt_connect_over_ws(connection, connect_packet);
    std::cout << "Connected over ws to " << profile.host << ':' << profile.port
              << profile.ws_path << " as " << profile.client_id << '\n';
  }

  std::cout << "Press Ctrl+C to disconnect." << '\n';
  keep_connection_open(connection, profile.transport, profile.keep_alive_seconds);
  send_disconnect_best_effort(connection, profile.transport);
  connection.close();
  std::cout << "Disconnected." << '\n';
  return 0;
}

int run_connect_command_with_retries(const TestClientProfile &profile) {
  for (uint32_t attempt_index = 0U;
       attempt_index <= profile.maximum_reconnect_times; ++attempt_index) {
    try {
      g_stop_requested.store(false);
      return run_connect_session(profile);
    } catch (const std::exception &exception) {
      std::cerr << "Connect attempt " << (attempt_index + 1U)
                << " failed: " << exception.what() << '\n';
    }

    if (attempt_index == profile.maximum_reconnect_times) {
      break;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(profile.reconnect_period_ms));
  }

  return 1;
}

int run_publish_command(const TestClientProfile &profile) {
  const ConnectPacket connect_packet = build_connect_packet_from_profile(profile);
  const PublishPacket publish_packet =
      build_publish_packet_from_profile_or_throw(profile);

  TcpConnection connection =
      ConnectionNegotiator::dial_tcp(profile.host, profile.port);
  StreamBuffer mqtt_stream_buffer;
  WebSocketFrameCodec ws_frame_codec;

  if (profile.transport == TestClientTransport::Mqtt) {
    (void)ConnectionNegotiator::negotiate(connection, connect_packet, 5000U);
  } else {
    perform_ws_upgrade(connection, profile);
    negotiate_mqtt_connect_over_ws(connection, connect_packet);
  }

  WriteBuffer publish_frame;
  encode_publish(publish_frame, publish_packet);
  write_transport_packet_or_throw(connection, profile.transport, publish_frame);

  if (publish_packet.qos == QoS::AtLeastOnce) {
    const uint16_t expected_packet_id = *publish_packet.packet_id;
    while (true) {
      const AnyPacket any_packet = read_next_transport_packet_or_throw(
          connection, profile.transport, mqtt_stream_buffer, ws_frame_codec,
          5000U);
      if (std::holds_alternative<PubackPacket>(any_packet)) {
        const PubackPacket &puback_packet = std::get<PubackPacket>(any_packet);
        if (puback_packet.packet_id == expected_packet_id) {
          if (is_error(puback_packet.reason_code)) {
            throw ClientException(ClientError::ProtocolError,
                                  "PUBACK returned error reason",
                                  puback_packet.reason_code);
          }
          break;
        }
      }
      if (std::holds_alternative<DisconnectPacket>(any_packet)) {
        const DisconnectPacket &disconnect_packet =
            std::get<DisconnectPacket>(any_packet);
        throw ClientException(ClientError::ProtocolError,
                              "Broker sent DISCONNECT during publish",
                              disconnect_packet.reason_code);
      }
    }
  } else if (publish_packet.qos == QoS::ExactlyOnce) {
    const uint16_t expected_packet_id = *publish_packet.packet_id;
    while (true) {
      const AnyPacket any_packet = read_next_transport_packet_or_throw(
          connection, profile.transport, mqtt_stream_buffer, ws_frame_codec,
          5000U);
      if (!std::holds_alternative<PubrecPacket>(any_packet)) {
        if (std::holds_alternative<DisconnectPacket>(any_packet)) {
          const DisconnectPacket &disconnect_packet =
              std::get<DisconnectPacket>(any_packet);
          throw ClientException(ClientError::ProtocolError,
                                "Broker sent DISCONNECT during QoS2 publish",
                                disconnect_packet.reason_code);
        }
        continue;
      }

      const PubrecPacket &pubrec_packet = std::get<PubrecPacket>(any_packet);
      if (pubrec_packet.packet_id != expected_packet_id) {
        continue;
      }
      if (is_error(pubrec_packet.reason_code)) {
        throw ClientException(ClientError::ProtocolError,
                              "PUBREC returned error reason",
                              pubrec_packet.reason_code);
      }

      WriteBuffer pubrel_frame;
      PubrelPacket pubrel_packet;
      pubrel_packet.packet_id = expected_packet_id;
      encode_pubrel(pubrel_frame, pubrel_packet);
      write_transport_packet_or_throw(connection, profile.transport,
                                      pubrel_frame);
      break;
    }

    while (true) {
      const AnyPacket any_packet = read_next_transport_packet_or_throw(
          connection, profile.transport, mqtt_stream_buffer, ws_frame_codec,
          5000U);
      if (std::holds_alternative<PubcompPacket>(any_packet)) {
        const PubcompPacket &pubcomp_packet = std::get<PubcompPacket>(any_packet);
        if (pubcomp_packet.packet_id == expected_packet_id) {
          if (is_error(pubcomp_packet.reason_code)) {
            throw ClientException(ClientError::ProtocolError,
                                  "PUBCOMP returned error reason",
                                  pubcomp_packet.reason_code);
          }
          break;
        }
      }
      if (std::holds_alternative<DisconnectPacket>(any_packet)) {
        const DisconnectPacket &disconnect_packet =
            std::get<DisconnectPacket>(any_packet);
        throw ClientException(ClientError::ProtocolError,
                              "Broker sent DISCONNECT during QoS2 completion",
                              disconnect_packet.reason_code);
      }
    }
  }

  send_disconnect_best_effort(connection, profile.transport);
  connection.close();

  std::cout << "Publish succeeded to topic " << publish_packet.topic.value
            << " with qos=" << static_cast<uint32_t>(profile.publish_qos)
            << '\n';
  return 0;
}

void print_profile_to_stdout(const TestClientProfile &profile) {
  std::cout << "host=" << profile.host << '\n';
  std::cout << "port=" << profile.port << '\n';
  std::cout << "transport=" << to_string(profile.transport) << '\n';
  std::cout << "ws_path=" << profile.ws_path << '\n';
  for (const std::string &header_line : profile.ws_headers) {
    std::cout << "ws_header=" << header_line << '\n';
  }
  std::cout << "client_id=" << profile.client_id << '\n';
  std::cout << "clean_start=" << (profile.clean_start ? "true" : "false")
            << '\n';
  std::cout << "keep_alive_seconds=" << profile.keep_alive_seconds << '\n';
  if (profile.username.has_value()) {
    std::cout << "username=" << *profile.username << '\n';
  }
  if (profile.password.has_value()) {
    std::cout << "password=<redacted>" << '\n';
  }
  std::cout << "reconnect_period_ms=" << profile.reconnect_period_ms << '\n';
  std::cout << "maximum_reconnect_times=" << profile.maximum_reconnect_times
            << '\n';

  std::cout << "session_expiry_interval_seconds="
            << profile.session_expiry_interval_seconds << '\n';
  std::cout << "receive_maximum=" << profile.receive_maximum << '\n';
  std::cout << "maximum_packet_size=" << profile.maximum_packet_size << '\n';
  std::cout << "topic_alias_maximum=" << profile.topic_alias_maximum << '\n';
  std::cout << "request_response_information="
            << (profile.request_response_information ? "true" : "false")
            << '\n';
  std::cout << "request_problem_information="
            << (profile.request_problem_information ? "true" : "false")
            << '\n';
  for (const auto &entry : profile.connect_user_properties) {
    std::cout << "connect_user_property=" << entry.first << '=' << entry.second
              << '\n';
  }
  if (profile.authentication_method.has_value()) {
    std::cout << "authentication_method=" << *profile.authentication_method
              << '\n';
  }
  if (profile.authentication_data.has_value()) {
    std::cout << "authentication_data=<redacted>" << '\n';
  }

  if (profile.will_topic.has_value()) {
    std::cout << "will_topic=" << *profile.will_topic << '\n';
  }
  std::cout << "will_payload=" << profile.will_payload << '\n';
  std::cout << "will_qos=" << static_cast<uint32_t>(profile.will_qos) << '\n';
  std::cout << "will_retain=" << (profile.will_retain ? "true" : "false")
            << '\n';
  std::cout << "will_delay_interval_seconds="
            << profile.will_delay_interval_seconds << '\n';
  if (profile.will_payload_format_indicator.has_value()) {
    std::cout << "will_payload_format_indicator="
              << static_cast<uint32_t>(*profile.will_payload_format_indicator)
              << '\n';
  }
  if (profile.will_message_expiry_interval_seconds.has_value()) {
    std::cout << "will_message_expiry_interval_seconds="
              << *profile.will_message_expiry_interval_seconds << '\n';
  }
  if (profile.will_content_type.has_value()) {
    std::cout << "will_content_type=" << *profile.will_content_type << '\n';
  }
  if (profile.will_response_topic.has_value()) {
    std::cout << "will_response_topic=" << *profile.will_response_topic << '\n';
  }
  if (profile.will_correlation_data.has_value()) {
    std::cout << "will_correlation_data=<redacted>" << '\n';
  }
  for (const auto &entry : profile.will_user_properties) {
    std::cout << "will_user_property=" << entry.first << '=' << entry.second
              << '\n';
  }

  if (profile.publish_topic.has_value()) {
    std::cout << "publish_topic=" << *profile.publish_topic << '\n';
  }
  std::cout << "publish_qos=" << static_cast<uint32_t>(profile.publish_qos)
            << '\n';
  std::cout << "publish_retain=" << (profile.publish_retain ? "true" : "false")
            << '\n';
  std::cout << "publish_dup=" << (profile.publish_dup ? "true" : "false")
            << '\n';
  if (profile.publish_payload.has_value()) {
    std::cout << "publish_payload=<configured>" << '\n';
  }
  std::cout << "publish_payload_stdin="
            << (profile.publish_payload_stdin ? "true" : "false") << '\n';
  std::cout << "publish_payload_stdin_multiline="
            << (profile.publish_payload_stdin_multiline ? "true" : "false")
            << '\n';
  if (profile.publish_payload_file.has_value()) {
    std::cout << "publish_payload_file=" << *profile.publish_payload_file
              << '\n';
  }
  std::cout << "publish_payload_encoding=" << profile.publish_payload_encoding
            << '\n';
  if (profile.publish_payload_format_indicator.has_value()) {
    std::cout << "publish_payload_format_indicator="
              << static_cast<uint32_t>(*profile.publish_payload_format_indicator)
              << '\n';
  }
  if (profile.publish_message_expiry_interval_seconds.has_value()) {
    std::cout << "publish_message_expiry_interval_seconds="
              << *profile.publish_message_expiry_interval_seconds << '\n';
  }
  if (profile.publish_topic_alias.has_value()) {
    std::cout << "publish_topic_alias=" << *profile.publish_topic_alias << '\n';
  }
  if (profile.publish_response_topic.has_value()) {
    std::cout << "publish_response_topic=" << *profile.publish_response_topic
              << '\n';
  }
  if (profile.publish_correlation_data.has_value()) {
    std::cout << "publish_correlation_data=<redacted>" << '\n';
  }
  std::cout << "publish_correlation_data_encoding="
            << profile.publish_correlation_data_encoding << '\n';
  if (profile.publish_subscription_identifier.has_value()) {
    std::cout << "publish_subscription_identifier="
              << *profile.publish_subscription_identifier << '\n';
  }
  if (profile.publish_content_type.has_value()) {
    std::cout << "publish_content_type=" << *profile.publish_content_type
              << '\n';
  }
  for (const auto &entry : profile.publish_user_properties) {
    std::cout << "publish_user_property=" << entry.first << '=' << entry.second
              << '\n';
  }
}

} // namespace
} // namespace mqtt

int main(const int argc, const char *argv[]) {
  try {
    const mqtt::TestClientCliOptions options =
        mqtt::parse_test_client_cli(argc, argv);

    if (options.command == mqtt::TestClientCommand::Help) {
      std::cout << mqtt::test_client_help_text();
      return 0;
    }

    const mqtt::TestClientProfile profile = mqtt::build_effective_profile(options);

    if (options.command == mqtt::TestClientCommand::ShowProfile) {
      mqtt::print_profile_to_stdout(profile);
      return 0;
    }

    if (options.command == mqtt::TestClientCommand::SaveProfile) {
      mqtt::save_test_client_profile_to_file(options.output_path, profile);
      std::cout << "Saved profile to " << options.output_path << '\n';
      return 0;
    }

    if (options.command == mqtt::TestClientCommand::Publish) {
      return mqtt::run_publish_command(profile);
    }

    mqtt::install_signal_handlers();
    return mqtt::run_connect_command_with_retries(profile);
  } catch (const std::exception &exception) {
    std::cerr << "yahatestclient: " << exception.what() << '\n';
    return 1;
  }
}
