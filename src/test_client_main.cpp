#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "client/connection_negotiator.h"
#include "client_api/client_api_error.h"
#include "client_api/client_config.h"
#include "codec/packet/connect_codec.h"
#include "codec/packet/control_codec.h"
#include "codec/packet_reader/packet_reader.h"
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

void write_ws_mqtt_packet(TcpConnection &connection,
                          std::span<const uint8_t> packet_bytes) {
  const std::vector<uint8_t> frame = encode_masked_binary_frame(packet_bytes);
  if (!connection.write(frame)) {
    throw ClientException(ClientError::WriteFailed,
                          "Failed to write MQTT frame over WebSocket");
  }
}

[[nodiscard]] std::string build_ws_upgrade_request(const TestClientProfile &profile) {
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
  write_ws_mqtt_packet(connection, connect_bytes);

  connection.set_receive_timeout(5000U);
  WebSocketFrameCodec frame_codec;
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

    frame_codec.append(
        std::span<const uint8_t>(receive_buffer.data(),
                                 static_cast<std::size_t>(bytes_read)));

    while (frame_codec.has_frame()) {
      const WsFrame frame = frame_codec.consume_frame();

      if (frame.opcode == WsOpcode::Ping) {
        const std::vector<uint8_t> pong_frame =
            encode_masked_frame(WsOpcode::Pong, frame.payload);
        (void)connection.write(pong_frame);
        continue;
      }
      if (frame.opcode == WsOpcode::Close) {
        throw ClientException(ClientError::ProtocolError,
                              "WebSocket closed during MQTT negotiation");
      }
      if (frame.opcode != WsOpcode::Binary) {
        continue;
      }

      ReadBuffer packet_buffer(frame.payload);
      const AnyPacket packet = read_packet(packet_buffer);
      if (!std::holds_alternative<ConnackPacket>(packet)) {
        throw ClientException(ClientError::ProtocolError,
                              "First MQTT packet over WebSocket is not CONNACK");
      }

      const ConnackPacket &connack_packet = std::get<ConnackPacket>(packet);
      if (is_error(connack_packet.reason_code)) {
        throw ClientException(ClientError::NegotiationRejected,
                              "Broker rejected CONNECT over WebSocket",
                              connack_packet.reason_code);
      }

      return;
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

      bool write_ok = false;
      if (transport == TestClientTransport::Mqtt) {
        write_ok = connection.write(ping_request);
      } else {
        const std::vector<uint8_t> ping_frame =
            encode_masked_binary_frame(ping_request);
        write_ok = connection.write(ping_frame);
      }

      if (!write_ok) {
        throw ClientException(ClientError::WriteFailed,
                              "Failed to send keep-alive ping request");
      }

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

  if (transport == TestClientTransport::Mqtt) {
    (void)connection.write(disconnect_bytes);
    return;
  }

  const std::vector<uint8_t> frame = encode_masked_binary_frame(disconnect_bytes);
  (void)connection.write(frame);
}

int run_connect_session(const TestClientProfile &profile) {
  const ClientConfig client_config = make_client_config_from_profile(profile);
  validate_client_config_or_throw(client_config);
  const ConnectPacket connect_packet = build_connect_packet(client_config);

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
      std::cerr << "Connect attempt " << (attempt_index + 1U) << " failed: "
                << exception.what() << '\n';
    }

    if (attempt_index == profile.maximum_reconnect_times) {
      break;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(profile.reconnect_period_ms));
  }

  return 1;
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

    mqtt::install_signal_handlers();
    return mqtt::run_connect_command_with_retries(profile);
  } catch (const std::exception &exception) {
    std::cerr << "yahatestclient: " << exception.what() << '\n';
    return 1;
  }
}
