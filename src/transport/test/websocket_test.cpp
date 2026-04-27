#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "network/tcp_connection.h"
#include "network/tcp_listener.h"
#include "transport/transport_error.h"
#include "transport/websocket_frame_codec.h"
#include "transport/websocket_handshake.h"
#include "transport/websocket_transport.h"

using namespace mqtt;

//
// Helpers

namespace {

/// Build a minimal valid HTTP WebSocket upgrade request.
std::vector<uint8_t>
make_valid_upgrade(std::string_view key = "dGhlIHNhbXBsZSBub25jZQ==") {
  const std::string req = "GET / HTTP/1.1\r\n"
                          "Host: localhost\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Key: " +
                          std::string(key) +
                          "\r\n"
                          "Sec-WebSocket-Version: 13\r\n"
                          "Sec-WebSocket-Protocol: mqtt\r\n"
                          "\r\n";
  return {req.begin(), req.end()};
}

/// Build a raw WebSocket frame (server-side, no mask).
std::vector<uint8_t> make_unmasked_binary_frame(std::vector<uint8_t> payload) {
  return WebSocketFrameCodec::encode_binary(payload);
}

/// Build a masked WebSocket frame (client-side style).
std::vector<uint8_t> make_masked_frame(WsOpcode opcode, bool fin,
                                       const std::vector<uint8_t> &payload,
                                       std::array<uint8_t, 4> mask) {
  std::vector<uint8_t> frame;
  const std::uint8_t byte0 = static_cast<std::uint8_t>(
      (fin ? 0x80U : 0x00U) | static_cast<std::uint8_t>(opcode));
  frame.push_back(byte0);

  const std::size_t plen = payload.size();
  if (plen <= 125U) {
    frame.push_back(static_cast<std::uint8_t>(0x80U | plen)); // MASK bit set
  } else if (plen <= 65535U) {
    frame.push_back(0x80U | 126U);
    frame.push_back(static_cast<uint8_t>((plen >> 8U) & 0xFFU));
    frame.push_back(static_cast<uint8_t>(plen & 0xFFU));
  } else {
    frame.push_back(0x80U | 127U);
    for (int shift = 56; shift >= 0; shift -= 8) {
      frame.push_back(static_cast<uint8_t>((plen >> shift) & 0xFFU));
    }
  }

  frame.insert(frame.end(), mask.begin(), mask.end());

  for (std::size_t idx = 0U; idx < plen; ++idx) {
    frame.push_back(payload[idx] ^ mask[idx % 4U]);
  }
  return frame;
}

void make_socket_pair(TcpConnection &client_conn, TcpConnection &server_conn) {
  auto listener = TcpListener::listen(0U, false);
  uint16_t bound_port = listener.port();

  std::unique_ptr<TcpConnection> accepted_conn;
  std::thread accept_thread([&] { accepted_conn = listener.accept(); });

  SocketHandle client_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  REQUIRE(client_socket != k_invalid_socket);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(bound_port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int ret = ::connect(client_socket, reinterpret_cast<const sockaddr *>(&addr),
                      sizeof(addr));
  REQUIRE(ret == 0);

  accept_thread.join();
  REQUIRE(accepted_conn != nullptr);

  client_conn = TcpConnection{client_socket};
  server_conn = std::move(*accepted_conn);
}

std::vector<uint8_t> read_once(TcpConnection &conn) {
  std::array<uint8_t, 4096U> raw{};
  std::ptrdiff_t num = conn.read(raw);
  REQUIRE(num > 0);
  return std::vector<uint8_t>(raw.begin(), raw.begin() + num);
}

} // namespace

//
// WebSocketHandshake tests

TEST_CASE("handshake_incomplete_on_partial_request", "[transport]") {
  WebSocketHandshake hsk;
  const std::string partial = "GET / HTTP/1.1\r\nHost: localhost\r\n";
  hsk.append(
      {reinterpret_cast<const uint8_t *>(partial.data()), partial.size()});
  CHECK(!hsk.is_complete());
}

TEST_CASE("handshake_complete_on_valid_upgrade", "[transport]") {
  WebSocketHandshake hsk;
  const auto req = make_valid_upgrade();
  hsk.append(req);
  CHECK(hsk.is_complete());
}

TEST_CASE("handshake_response_correct_accept_key", "[transport]") {
  // RFC 6455 §1.3 test vector.
  WebSocketHandshake hsk;
  const auto req = make_valid_upgrade("dGhlIHNhbXBsZSBub25jZQ==");
  hsk.append(req);
  REQUIRE(hsk.is_complete());
  const auto resp = hsk.build_response();
  const std::string resp_str(resp.begin(), resp.end());
  CHECK(resp_str.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
}

TEST_CASE("handshake_response_contains_101", "[transport]") {
  WebSocketHandshake hsk;
  hsk.append(make_valid_upgrade());
  REQUIRE(hsk.is_complete());
  const auto resp = hsk.build_response();
  const std::string resp_str(resp.begin(), resp.end());
  CHECK(resp_str.find("101 Switching Protocols") != std::string::npos);
}

TEST_CASE("handshake_rejects_missing_upgrade_header", "[transport]") {
  WebSocketHandshake hsk;
  const std::string req = "GET / HTTP/1.1\r\n"
                          "Host: localhost\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Key: abc123==\r\n"
                          "Sec-WebSocket-Version: 13\r\n"
                          "\r\n";
  const std::span<const uint8_t> span{
      reinterpret_cast<const uint8_t *>(req.data()), req.size()};
  CHECK_THROWS_AS(hsk.append(span), TransportException);
}

TEST_CASE("handshake_rejects_wrong_version", "[transport]") {
  WebSocketHandshake hsk;
  const std::string req = "GET / HTTP/1.1\r\n"
                          "Host: localhost\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Key: abc123==\r\n"
                          "Sec-WebSocket-Version: 8\r\n"
                          "\r\n";
  const std::span<const uint8_t> span{
      reinterpret_cast<const uint8_t *>(req.data()), req.size()};
  CHECK_THROWS_AS(hsk.append(span), TransportException);
}

TEST_CASE("handshake_rejects_missing_key", "[transport]") {
  WebSocketHandshake hsk;
  const std::string req = "GET / HTTP/1.1\r\n"
                          "Host: localhost\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Version: 13\r\n"
                          "\r\n";
  const std::span<const uint8_t> span{
      reinterpret_cast<const uint8_t *>(req.data()), req.size()};
  CHECK_THROWS_AS(hsk.append(span), TransportException);
}

TEST_CASE("handshake_rejects_missing_connection_header", "[transport]") {
  WebSocketHandshake hsk;
  const std::string req = "GET / HTTP/1.1\r\n"
                          "Host: localhost\r\n"
                          "Upgrade: websocket\r\n"
                          "Sec-WebSocket-Key: abc123==\r\n"
                          "Sec-WebSocket-Version: 13\r\n"
                          "\r\n";
  const std::span<const uint8_t> span{
      reinterpret_cast<const uint8_t *>(req.data()), req.size()};
  CHECK_THROWS_AS(hsk.append(span), TransportException);
}

TEST_CASE("handshake_rejects_missing_subprotocol_header", "[transport]") {
  WebSocketHandshake hsk;
  const std::string req = "GET / HTTP/1.1\r\n"
                          "Host: localhost\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Key: abc123==\r\n"
                          "Sec-WebSocket-Version: 13\r\n"
                          "\r\n";
  const std::span<const uint8_t> span{
      reinterpret_cast<const uint8_t *>(req.data()), req.size()};
  CHECK_THROWS_AS(hsk.append(span), TransportException);
}

TEST_CASE("handshake_accepts_mqtt_subprotocol_header", "[transport]") {
  WebSocketHandshake hsk;
  const std::string req = "GET / HTTP/1.1\r\n"
                          "Host: localhost\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Key: abc123==\r\n"
                          "Sec-WebSocket-Version: 13\r\n"
                          "Sec-WebSocket-Protocol: mqtt\r\n"
                          "\r\n";
  const std::span<const uint8_t> span{
      reinterpret_cast<const uint8_t *>(req.data()), req.size()};
  hsk.append(span);
  CHECK(hsk.is_complete());
}

TEST_CASE("handshake_build_response_before_complete_throws", "[transport]") {
  WebSocketHandshake hsk;
  CHECK_THROWS_AS(hsk.build_response(), std::logic_error);
}

TEST_CASE("handshake_second_append_noop_after_complete", "[transport]") {
  WebSocketHandshake hsk;
  hsk.append(make_valid_upgrade());
  REQUIRE(hsk.is_complete());
  const auto resp1 = hsk.build_response();
  // Appending more bytes after completion should not change anything.
  const std::string extra = "garbage";
  hsk.append({reinterpret_cast<const uint8_t *>(extra.data()), extra.size()});
  CHECK(hsk.is_complete());
  const auto resp2 = hsk.build_response();
  CHECK(resp1 == resp2);
}

//
// WebSocketFrameCodec — decode tests

TEST_CASE("frame_no_frame_on_empty", "[transport]") {
  WebSocketFrameCodec codec;
  CHECK(!codec.has_frame());
}

TEST_CASE("frame_decode_small_unmasked", "[transport]") {
  WebSocketFrameCodec codec;
  const std::vector<uint8_t> payload = {0x01U, 0x02U, 0x03U};
  codec.append(make_unmasked_binary_frame(payload));
  REQUIRE(codec.has_frame());
  const WsFrame frm = codec.consume_frame();
  CHECK(frm.fin == true);
  CHECK(frm.opcode == WsOpcode::Binary);
  CHECK(frm.payload == payload);
}

TEST_CASE("frame_decode_16bit_length", "[transport]") {
  WebSocketFrameCodec codec;
  // 200 bytes → requires 16-bit extended length field.
  const std::vector<uint8_t> payload(200U, 0xABU);
  codec.append(make_unmasked_binary_frame(payload));
  REQUIRE(codec.has_frame());
  CHECK(codec.consume_frame().payload == payload);
}

TEST_CASE("frame_decode_64bit_length", "[transport]") {
  // Manually build a frame with the 127 + 8-byte length encoding
  // but a small actual payload to keep the test fast.
  WebSocketFrameCodec codec;
  const std::vector<uint8_t> payload = {0x10U, 0x20U, 0x30U};

  std::vector<uint8_t> frame;
  frame.push_back(0x82U); // FIN + Binary
  frame.push_back(127U);  // 8-byte length follows, no mask
  const std::uint64_t plen = payload.size();
  for (int shift = 56; shift >= 0; shift -= 8) {
    frame.push_back(static_cast<uint8_t>((plen >> shift) & 0xFFU));
  }
  frame.insert(frame.end(), payload.begin(), payload.end());

  codec.append(frame);
  REQUIRE(codec.has_frame());
  CHECK(codec.consume_frame().payload == payload);
}

TEST_CASE("frame_decode_masked", "[transport]") {
  WebSocketFrameCodec codec;
  const std::vector<uint8_t> original = {0xAAU, 0xBBU, 0xCCU, 0xDDU};
  const std::array<uint8_t, 4> mask = {0x12U, 0x34U, 0x56U, 0x78U};

  const auto raw = make_masked_frame(WsOpcode::Binary, true, original, mask);
  codec.append(raw);
  REQUIRE(codec.has_frame());
  CHECK(codec.consume_frame().payload == original);
}

TEST_CASE("frame_decode_fragmented_delivery", "[transport]") {
  WebSocketFrameCodec codec;
  const std::vector<uint8_t> payload = {0x01U, 0x02U};
  const auto frame_bytes = make_unmasked_binary_frame(payload);

  // Deliver first half.
  const std::size_t mid = frame_bytes.size() / 2U;
  codec.append({frame_bytes.data(), mid});
  CHECK(!codec.has_frame());

  // Deliver second half.
  codec.append({frame_bytes.data() + mid, frame_bytes.size() - mid});
  CHECK(codec.has_frame());
  CHECK(codec.consume_frame().payload == payload);
}

TEST_CASE("frame_decode_multiple_frames", "[transport]") {
  WebSocketFrameCodec codec;
  const std::vector<uint8_t> pay1 = {0x01U};
  const std::vector<uint8_t> pay2 = {0x02U, 0x03U};

  auto combined = make_unmasked_binary_frame(pay1);
  const auto frame_two = make_unmasked_binary_frame(pay2);
  combined.insert(combined.end(), frame_two.begin(), frame_two.end());

  codec.append(combined);
  REQUIRE(codec.has_frame());
  CHECK(codec.consume_frame().payload == pay1);
  REQUIRE(codec.has_frame());
  CHECK(codec.consume_frame().payload == pay2);
}

TEST_CASE("frame_decode_many_frames_fifo_order", "[transport]") {
  WebSocketFrameCodec codec;

  std::vector<std::vector<uint8_t>> expected_payloads;
  expected_payloads.reserve(256U);

  std::vector<uint8_t> combined;
  for (std::size_t idx = 0U; idx < 256U; ++idx) {
    std::vector<uint8_t> payload = {
        static_cast<uint8_t>(idx & 0xFFU),
        static_cast<uint8_t>((idx * 3U) & 0xFFU),
    };
    expected_payloads.push_back(payload);

    const auto frame = make_unmasked_binary_frame(payload);
    combined.insert(combined.end(), frame.begin(), frame.end());
  }

  codec.append(combined);
  for (const auto &expected : expected_payloads) {
    REQUIRE(codec.has_frame());
    CHECK(codec.consume_frame().payload == expected);
  }
  CHECK(!codec.has_frame());
}

TEST_CASE("frame_decode_ping_opcode", "[transport]") {
  WebSocketFrameCodec codec;
  const std::vector<uint8_t> ping_payload = {0x70U, 0x69U, 0x6EU,
                                             0x67U}; // "ping"
  const auto ping_bytes =
      WebSocketFrameCodec::encode_control(WsOpcode::Ping, ping_payload);
  codec.append(ping_bytes);
  REQUIRE(codec.has_frame());
  const WsFrame frm = codec.consume_frame();
  CHECK(frm.opcode == WsOpcode::Ping);
  CHECK(frm.payload == ping_payload);
}

TEST_CASE("frame_decode_close_opcode", "[transport]") {
  WebSocketFrameCodec codec;
  const auto close_bytes = WebSocketFrameCodec::encode_control(WsOpcode::Close);
  codec.append(close_bytes);
  REQUIRE(codec.has_frame());
  CHECK(codec.consume_frame().opcode == WsOpcode::Close);
}

TEST_CASE("frame_decode_fin_flag", "[transport]") {
  WebSocketFrameCodec codec;
  // Build a frame with FIN=0.
  const std::vector<uint8_t> payload = {0x01U};
  const auto raw =
      make_masked_frame(WsOpcode::Binary, false, payload, {0U, 0U, 0U, 0U});
  codec.append(raw);
  REQUIRE(codec.has_frame());
  CHECK(codec.consume_frame().fin == false);
}

TEST_CASE("frame_consume_empty_throws", "[transport]") {
  WebSocketFrameCodec codec;
  CHECK_THROWS_AS(codec.consume_frame(), std::logic_error);
}

TEST_CASE("frame_rejects_rsv_bits", "[transport]") {
  WebSocketFrameCodec codec;
  // Byte 0: FIN + RSV1 set + Binary (0b11000010 = 0xC2)
  const std::vector<uint8_t> bad_frame = {0xC2U, 0x01U, 0xFFU};
  CHECK_THROWS_AS(codec.append(bad_frame), TransportException);
}

TEST_CASE("frame_rejects_unknown_opcode", "[transport]") {
  WebSocketFrameCodec codec;
  // Opcode 0x3 is reserved/unknown (FIN=1, opcode=3 → 0x83, len=0)
  const std::vector<uint8_t> bad_frame = {0x83U, 0x00U};
  CHECK_THROWS_AS(codec.append(bad_frame), TransportException);
}

//
// WebSocketFrameCodec — encode tests

TEST_CASE("frame_encode_binary_small", "[transport]") {
  const std::vector<uint8_t> payload = {0x01U, 0x02U, 0x03U};
  const auto encoded = WebSocketFrameCodec::encode_binary(payload);
  // byte 0: 0x82 (FIN + Binary), byte 1: 0x03 (length = 3), then payload
  REQUIRE(encoded.size() == 5U);
  CHECK(encoded[0] == 0x82U);
  CHECK(encoded[1] == 0x03U);
  CHECK(std::vector<uint8_t>(encoded.begin() + 2, encoded.end()) == payload);
}

TEST_CASE("frame_encode_binary_16bit", "[transport]") {
  const std::vector<uint8_t> payload(126U, 0x55U);
  const auto encoded = WebSocketFrameCodec::encode_binary(payload);
  // byte 0: 0x82, byte 1: 126 (0x7E), bytes 2-3: length big-endian
  REQUIRE(encoded.size() == 2U + 2U + 126U);
  CHECK(encoded[0] == 0x82U);
  CHECK(encoded[1] == 126U);
  const std::uint16_t len = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(encoded[2]) << 8U) | encoded[3]);
  CHECK(len == 126U);
}

TEST_CASE("frame_encode_binary_roundtrip", "[transport]") {
  const std::vector<uint8_t> payload = {0xDEU, 0xADU, 0xBEU, 0xEFU};
  const auto encoded = WebSocketFrameCodec::encode_binary(payload);

  WebSocketFrameCodec codec;
  codec.append(encoded);
  REQUIRE(codec.has_frame());
  const WsFrame frm = codec.consume_frame();
  CHECK(frm.opcode == WsOpcode::Binary);
  CHECK(frm.payload == payload);
}

TEST_CASE("frame_encode_control_ping", "[transport]") {
  const auto encoded = WebSocketFrameCodec::encode_control(WsOpcode::Ping);
  REQUIRE(!encoded.empty());
  CHECK((encoded[0] & 0x80U) != 0U); // FIN set
  CHECK((encoded[0] & 0x0FU) == static_cast<std::uint8_t>(WsOpcode::Ping));
}

TEST_CASE("frame_encode_control_close", "[transport]") {
  const auto encoded = WebSocketFrameCodec::encode_control(WsOpcode::Close);
  REQUIRE(!encoded.empty());
  CHECK((encoded[0] & 0x0FU) == static_cast<std::uint8_t>(WsOpcode::Close));
}

TEST_CASE("ws_transport_encode_frame_small", "[transport]") {
  const std::vector<uint8_t> payload = {0x01U, 0x02U, 0x03U};
  const auto encoded = WebSocketTransport::encode_frame(payload);

  WebSocketFrameCodec codec;
  codec.append(encoded);
  REQUIRE(codec.has_frame());
  const WsFrame frm = codec.consume_frame();
  CHECK(frm.opcode == WsOpcode::Binary);
  CHECK(frm.payload == payload);
}

TEST_CASE("ws_transport_encode_frame_126_boundary", "[transport]") {
  const std::vector<uint8_t> payload(126U, 0x44U);
  const auto encoded = WebSocketTransport::encode_frame(payload);

  REQUIRE(encoded.size() == 2U + 2U + payload.size());
  CHECK(encoded[0] == 0x82U);
  CHECK(encoded[1] == 126U);
  const std::uint16_t len = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(encoded[2]) << 8U) | encoded[3]);
  CHECK(len == 126U);
}

TEST_CASE("ws_transport_encode_frame_empty", "[transport]") {
  const auto encoded =
      WebSocketTransport::encode_frame(std::span<const uint8_t>{});
  REQUIRE(encoded.size() == 2U);
  CHECK(encoded[0] == 0x82U);
  CHECK(encoded[1] == 0x00U);
}

TEST_CASE("ws_transport_encode_frame_roundtrip", "[transport]") {
  const std::vector<uint8_t> payload = {0xDEU, 0xADU, 0xBEU, 0xEFU};
  const auto encoded = WebSocketTransport::encode_frame(payload);

  WebSocketFrameCodec codec;
  codec.append(encoded);
  REQUIRE(codec.has_frame());
  const WsFrame frm = codec.consume_frame();
  CHECK(frm.opcode == WsOpcode::Binary);
  CHECK(frm.payload == payload);
}

TEST_CASE("ws_transport_handshake_and_read_binary", "[transport]") {
  TcpConnection client_conn{k_invalid_socket};
  TcpConnection server_conn{k_invalid_socket};
  make_socket_pair(client_conn, server_conn);

  std::vector<uint8_t> received_payload;
  bool timed_out = false;
  bool eof_seen = false;

  std::thread server_thread([&] {
    WebSocketTransport transport(server_conn);
    transport.set_receive_timeout(1000U);
    WsReadChunk chunk = transport.read_chunk();
    received_payload = chunk.data;
    timed_out = chunk.timed_out;
    eof_seen = chunk.eof;
  });

  const auto request = make_valid_upgrade();
  REQUIRE(client_conn.write(request));

  const auto handshake_resp = read_once(client_conn);
  const std::string handshake_text(handshake_resp.begin(),
                                   handshake_resp.end());
  CHECK(handshake_text.find("101 Switching Protocols") != std::string::npos);

  const std::vector<uint8_t> mqtt_payload = {0x10U, 0x00U};
  const auto ws_frame = WebSocketFrameCodec::encode_binary(mqtt_payload);
  REQUIRE(client_conn.write(ws_frame));

  server_thread.join();

  CHECK(received_payload == mqtt_payload);
  CHECK(timed_out == false);
  CHECK(eof_seen == false);
}

TEST_CASE("ws_transport_write_frame_to_client", "[transport]") {
  TcpConnection client_conn{k_invalid_socket};
  TcpConnection server_conn{k_invalid_socket};
  make_socket_pair(client_conn, server_conn);
  client_conn.set_receive_timeout(1500U);

  const std::vector<uint8_t> mqtt_payload = {0x30U, 0x03U, 0x01U, 0x02U, 0x03U};

  std::thread server_thread([&] {
    WebSocketTransport transport(server_conn);
    REQUIRE(transport.write_frame(mqtt_payload));
  });

  const auto request = make_valid_upgrade();
  REQUIRE(client_conn.write(request));
  const auto handshake_and_maybe_frame = read_once(client_conn);
  const std::string handshake_text(handshake_and_maybe_frame.begin(),
                                   handshake_and_maybe_frame.end());
  CHECK(handshake_text.find("101 Switching Protocols") != std::string::npos);

  std::vector<uint8_t> encoded;
  const std::size_t header_end = handshake_text.find("\r\n\r\n");
  if (header_end != std::string::npos) {
    const std::size_t payload_offset = header_end + 4U;
    if (payload_offset < handshake_and_maybe_frame.size()) {
      encoded.assign(handshake_and_maybe_frame.begin() +
                         static_cast<std::ptrdiff_t>(payload_offset),
                     handshake_and_maybe_frame.end());
    }
  }
  if (encoded.empty()) {
    encoded = read_once(client_conn);
  }

  server_thread.join();

  WebSocketFrameCodec codec;
  codec.append(encoded);
  REQUIRE(codec.has_frame());
  const WsFrame frm = codec.consume_frame();
  CHECK(frm.opcode == WsOpcode::Binary);
  CHECK(frm.payload == mqtt_payload);
}

TEST_CASE("ws_transport_ping_gets_pong", "[transport]") {
  TcpConnection client_conn{k_invalid_socket};
  TcpConnection server_conn{k_invalid_socket};
  make_socket_pair(client_conn, server_conn);

  std::thread server_thread([&] {
    WebSocketTransport transport(server_conn);
    transport.set_receive_timeout(1000U);
    (void)transport.read_chunk();
  });

  const auto request = make_valid_upgrade();
  REQUIRE(client_conn.write(request));
  (void)read_once(client_conn); // handshake response

  const std::vector<uint8_t> ping_payload = {0x61U, 0x62U, 0x63U};
  const auto ping_frame =
      WebSocketFrameCodec::encode_control(WsOpcode::Ping, ping_payload);
  REQUIRE(client_conn.write(ping_frame));

  const auto pong_raw = read_once(client_conn);

  server_thread.join();

  WebSocketFrameCodec codec;
  codec.append(pong_raw);
  REQUIRE(codec.has_frame());
  const WsFrame pong = codec.consume_frame();
  CHECK(pong.opcode == WsOpcode::Pong);
  CHECK(pong.payload == ping_payload);
}

TEST_CASE("ws_transport_reassembles_fragmented_binary_message", "[transport]") {
  TcpConnection client_conn{k_invalid_socket};
  TcpConnection server_conn{k_invalid_socket};
  make_socket_pair(client_conn, server_conn);

  std::vector<uint8_t> decoded_payload;

  std::thread server_thread([&] {
    WebSocketTransport transport(server_conn);
    transport.set_receive_timeout(1000U);
    for (std::size_t iteration = 0U; iteration < 3U; ++iteration) {
      WsReadChunk chunk = transport.read_chunk();
      if (!chunk.data.empty()) {
        decoded_payload = std::move(chunk.data);
        return;
      }
      if (chunk.eof) {
        return;
      }
    }
  });

  const auto request = make_valid_upgrade();
  REQUIRE(client_conn.write(request));
  (void)read_once(client_conn); // handshake response

  const std::vector<uint8_t> part_one = {0x10U, 0x11U, 0x12U};
  const std::vector<uint8_t> part_two = {0x13U, 0x14U, 0x15U};
  const auto frame_one =
      make_masked_frame(WsOpcode::Binary, false, part_one, {1U, 2U, 3U, 4U});
  const auto frame_two = make_masked_frame(WsOpcode::Continuation, true,
                                           part_two, {4U, 3U, 2U, 1U});

  REQUIRE(client_conn.write(frame_one));
  REQUIRE(client_conn.write(frame_two));

  server_thread.join();

  const std::vector<uint8_t> expected = {0x10U, 0x11U, 0x12U,
                                         0x13U, 0x14U, 0x15U};
  CHECK(decoded_payload == expected);
}

TEST_CASE("ws_transport_text_frame_sets_eof", "[transport]") {
  TcpConnection client_conn{k_invalid_socket};
  TcpConnection server_conn{k_invalid_socket};
  make_socket_pair(client_conn, server_conn);

  bool eof_seen = false;

  std::thread server_thread([&] {
    WebSocketTransport transport(server_conn);
    transport.set_receive_timeout(1000U);
    WsReadChunk chunk = transport.read_chunk();
    eof_seen = chunk.eof;
  });

  const auto request = make_valid_upgrade();
  REQUIRE(client_conn.write(request));
  (void)read_once(client_conn); // handshake response

  const std::vector<uint8_t> text_payload = {'M', 'Q', 'T', 'T'};
  const auto text_frame =
      make_masked_frame(WsOpcode::Text, true, text_payload, {0U, 1U, 2U, 3U});
  REQUIRE(client_conn.write(text_frame));

  server_thread.join();

  CHECK(eof_seen == true);
}

TEST_CASE("ws_transport_close_sets_eof", "[transport]") {
  TcpConnection client_conn{k_invalid_socket};
  TcpConnection server_conn{k_invalid_socket};
  make_socket_pair(client_conn, server_conn);

  bool eof_seen = false;

  std::thread server_thread([&] {
    WebSocketTransport transport(server_conn);
    transport.set_receive_timeout(1000U);
    WsReadChunk chunk = transport.read_chunk();
    eof_seen = chunk.eof;
  });

  const auto request = make_valid_upgrade();
  REQUIRE(client_conn.write(request));
  (void)read_once(client_conn); // handshake response

  const auto close_frame = WebSocketFrameCodec::encode_control(WsOpcode::Close);
  REQUIRE(client_conn.write(close_frame));

  server_thread.join();

  CHECK(eof_seen == true);
}

TEST_CASE("ws_transport_constructor_throws_on_closed_socket", "[transport]") {
  TcpConnection client_conn{k_invalid_socket};
  TcpConnection server_conn{k_invalid_socket};
  make_socket_pair(client_conn, server_conn);

  client_conn.close();

  CHECK_THROWS_AS((void)WebSocketTransport(server_conn), TransportException);
}

TEST_CASE("ws_transport_read_chunk_timeout_sets_timed_out", "[transport]") {
  TcpConnection client_conn{k_invalid_socket};
  TcpConnection server_conn{k_invalid_socket};
  make_socket_pair(client_conn, server_conn);

  bool timed_out_seen = false;
  bool eof_seen = true;

  std::thread server_thread([&] {
    WebSocketTransport transport(server_conn);
    transport.set_receive_timeout(30U);
    WsReadChunk chunk = transport.read_chunk();
    timed_out_seen = chunk.timed_out;
    eof_seen = chunk.eof;
  });

  const auto request = make_valid_upgrade();
  REQUIRE(client_conn.write(request));
  (void)read_once(client_conn); // handshake response

  server_thread.join();

  CHECK(timed_out_seen == true);
  CHECK(eof_seen == false);
}

TEST_CASE("ws_transport_read_chunk_eof_on_tcp_close", "[transport]") {
  TcpConnection client_conn{k_invalid_socket};
  TcpConnection server_conn{k_invalid_socket};
  make_socket_pair(client_conn, server_conn);

  bool eof_seen = false;
  bool timed_out_seen = true;

  std::thread server_thread([&] {
    WebSocketTransport transport(server_conn);
    transport.set_receive_timeout(1000U);
    WsReadChunk chunk = transport.read_chunk();
    eof_seen = chunk.eof;
    timed_out_seen = chunk.timed_out;
  });

  const auto request = make_valid_upgrade();
  REQUIRE(client_conn.write(request));
  (void)read_once(client_conn); // handshake response

  client_conn.close();

  server_thread.join();

  CHECK(eof_seen == true);
  CHECK(timed_out_seen == false);
}

TEST_CASE("ws_transport_read_chunk_invalid_frame_sets_eof", "[transport]") {
  TcpConnection client_conn{k_invalid_socket};
  TcpConnection server_conn{k_invalid_socket};
  make_socket_pair(client_conn, server_conn);

  bool eof_seen = false;
  bool timed_out_seen = true;

  std::thread server_thread([&] {
    WebSocketTransport transport(server_conn);
    transport.set_receive_timeout(1000U);
    WsReadChunk chunk = transport.read_chunk();
    eof_seen = chunk.eof;
    timed_out_seen = chunk.timed_out;
  });

  const auto request = make_valid_upgrade();
  REQUIRE(client_conn.write(request));
  (void)read_once(client_conn); // handshake response

  const std::vector<uint8_t> invalid_frame = {0x83U, 0x00U};
  REQUIRE(client_conn.write(invalid_frame));

  server_thread.join();

  CHECK(eof_seen == true);
  CHECK(timed_out_seen == false);
}

TEST_CASE("ws_transport_tcp_returns_underlying_connection", "[transport]") {
  TcpConnection client_conn{k_invalid_socket};
  TcpConnection server_conn{k_invalid_socket};
  make_socket_pair(client_conn, server_conn);

  bool same_handle = false;

  std::thread server_thread([&] {
    WebSocketTransport transport(server_conn);
    same_handle = (transport.tcp().fd() == server_conn.fd());
  });

  const auto request = make_valid_upgrade();
  REQUIRE(client_conn.write(request));
  (void)read_once(client_conn); // handshake response

  server_thread.join();

  CHECK(same_handle == true);
}

TEST_CASE("frame_encode_binary_64bit_roundtrip", "[transport]") {
  // 65536 bytes → triggers the > 65535 encoder path (127 + 8-byte length).
  const std::vector<uint8_t> payload(65536U, 0x7FU);
  const auto encoded = WebSocketFrameCodec::encode_binary(payload);
  REQUIRE(encoded.size() == 2U + 8U + 65536U);
  CHECK(encoded[0] == 0x82U); // FIN + Binary
  CHECK(encoded[1] == 127U);  // 8-byte extended length marker

  WebSocketFrameCodec codec;
  codec.append(encoded);
  REQUIRE(codec.has_frame());
  CHECK(codec.consume_frame().payload == payload);
}

TEST_CASE("transport_exception_code_accessible", "[transport]") {
  // Verify that TransportException::code() returns the correct error code.
  WebSocketFrameCodec codec;
  try {
    const std::vector<uint8_t> bad_frame = {0x83U, 0x00U}; // unknown opcode
    codec.append(bad_frame);
    FAIL("Expected TransportException");
  } catch (const TransportException &exc) {
    CHECK(exc.code() == TransportError::InvalidOpcode);
  }
}
