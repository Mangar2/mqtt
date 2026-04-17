/**
 * @file client_handler_test.cpp
 * @brief Integration tests for ClientHandler (Module 17).
 *
 * Each test starts a real Broker on a loopback port, connects a raw
 * Winsock/POSIX socket, speaks MQTT 5.0 wire protocol, and validates
 * the server's responses.
 */

#include <catch2/catch_test_macros.hpp>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include "broker/broker.h"
#include "broker/broker_config.h"
#include "codec/packet/connect_codec.h"
#include "codec/packet/control_codec.h"
#include "codec/packet/publish_codec.h"
#include "codec/packet/subscribe_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "codec/read_buffer.h"
#include "codec/write_buffer.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/packet/control_packets.h"
#include "data_model/packet/publish_packets.h"
#include "data_model/packet/subscribe_packets.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/reason_code/reason_code.h"
#include "network/tcp_connection.h"

using namespace mqtt;
using namespace std::chrono_literals;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers

namespace {

constexpr uint16_t k_test_mqtt_port = 19001U;

BrokerConfig make_test_config() {
  BrokerConfig cfg;
  cfg.mqtt_port = k_test_mqtt_port;
  cfg.ws_port = 0U;
  cfg.allow_anonymous = true;
  cfg.persistence_enabled = false;
  cfg.receive_maximum = 65535U;
  cfg.topic_alias_maximum = 0U;
  return cfg;
}

/// Connect a raw TCP socket to loopback:port, returning its handle.
[[nodiscard]] SocketHandle raw_connect(uint16_t port) {
  SocketHandle sfd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sfd == k_invalid_socket) {
    return k_invalid_socket;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (::connect(sfd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) !=
      0) {
#ifdef _WIN32
    ::closesocket(sfd);
#else
    ::close(sfd);
#endif
    return k_invalid_socket;
  }
  return sfd;
}

/// Platform-safe way to close a raw socket.
void raw_close(SocketHandle sfd) {
  if (sfd == k_invalid_socket) {
    return;
  }
#ifdef _WIN32
  ::shutdown(sfd, SD_BOTH);
  ::closesocket(sfd);
#else
  ::shutdown(sfd, SHUT_RDWR);
  ::close(sfd);
#endif
}

/// Send all bytes of buf over the raw socket.
bool raw_send(SocketHandle sfd, const std::vector<uint8_t> &buf) {
  std::size_t sent = 0;
  while (sent < buf.size()) {
#ifdef _WIN32
    int ret = ::send(sfd, reinterpret_cast<const char *>(buf.data() + sent),
                     static_cast<int>(buf.size() - sent), 0);
#else
    std::ptrdiff_t ret =
        ::send(sfd, buf.data() + sent, buf.size() - sent, MSG_NOSIGNAL);
#endif
    if (ret <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(ret);
  }
  return true;
}

/// Receive exactly n bytes from the raw socket (blocking).
[[nodiscard]] std::vector<uint8_t> raw_recv_n(SocketHandle sfd, std::size_t n) {
  std::vector<uint8_t> buf(n, 0U);
  std::size_t received = 0;
  while (received < n) {
#ifdef _WIN32
    int ret = ::recv(sfd, reinterpret_cast<char *>(buf.data() + received),
                     static_cast<int>(n - received), 0);
#else
    std::ptrdiff_t ret = ::recv(sfd, buf.data() + received, n - received, 0);
#endif
    if (ret <= 0) {
      return {};
    }
    received += static_cast<std::size_t>(ret);
  }
  return buf;
}

/// Read one complete MQTT packet from the raw socket.
[[nodiscard]] std::vector<uint8_t> raw_recv_packet(SocketHandle sfd) {
  // Read first 2 bytes: type + first RL byte
  auto hdr = raw_recv_n(sfd, 2U);
  if (hdr.size() < 2U) {
    return {};
  }

  // Decode variable-byte remaining length
  uint32_t rl = 0U;
  uint32_t shift = 0U;
  std::size_t rl_bytes = 0U;
  uint8_t byte_val = hdr[1];
  rl = static_cast<uint32_t>(byte_val & 0x7FU);
  rl_bytes = 1U;
  while ((byte_val & 0x80U) != 0U) {
    auto ext = raw_recv_n(sfd, 1U);
    if (ext.empty()) {
      return {};
    }
    byte_val = ext[0];
    shift += 7U;
    rl |= static_cast<uint32_t>(byte_val & 0x7FU) << shift;
    ++rl_bytes;
    if (rl_bytes > 4U) {
      return {};
    }
  }

  // Read remaining_length bytes
  std::vector<uint8_t> pkt;
  pkt.push_back(hdr[0]);
  pkt.push_back(hdr[1]);
  for (std::size_t extra = 1U; extra < rl_bytes; ++extra) {
    auto eb = raw_recv_n(sfd, 1U);
    if (eb.empty()) {
      return {};
    }
    pkt.push_back(eb[0]);
  }
  auto payload = raw_recv_n(sfd, static_cast<std::size_t>(rl));
  pkt.insert(pkt.end(), payload.begin(), payload.end());
  return pkt;
}

/// Encode a minimal CONNECT packet.
[[nodiscard]] std::vector<uint8_t> make_connect(const std::string &cid,
                                                bool clean_start = true) {
  ConnectPacket pkt;
  pkt.client_id.value = cid;
  pkt.clean_start = clean_start;
  pkt.keep_alive = 0U;
  WriteBuffer buf;
  encode_connect(buf, pkt);
  return buf;
}

/// Decode an AnyPacket from raw bytes.
[[nodiscard]] AnyPacket decode_any(const std::vector<uint8_t> &raw) {
  auto rbuf = ReadBuffer{std::span<const uint8_t>(raw)};
  return read_packet(rbuf);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Tests

TEST_CASE("client_handler_connect_receives_connack", "[client_handler]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  // Give accept thread a moment to start
  std::this_thread::sleep_for(10ms);

  SocketHandle sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(sfd != k_invalid_socket);

  REQUIRE(raw_send(sfd, make_connect("test-conn-1")));

  auto raw = raw_recv_packet(sfd);
  REQUIRE(!raw.empty());

  auto pkt = decode_any(raw);
  REQUIRE(std::holds_alternative<ConnackPacket>(pkt));
  auto &connack = std::get<ConnackPacket>(pkt);
  CHECK(connack.reason_code == ReasonCode::Success);
  CHECK(connack.session_present == false);

  raw_close(sfd);
  broker.shutdown();
}

TEST_CASE("client_handler_non_connect_first_packet_closes",
          "[client_handler]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  std::this_thread::sleep_for(10ms);

  SocketHandle sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(sfd != k_invalid_socket);

  // Send PINGREQ instead of CONNECT
  WriteBuffer ping_buf;
  encode_pingreq(ping_buf);
  REQUIRE(raw_send(sfd, ping_buf));

  // Server should send DISCONNECT and close
  auto raw = raw_recv_packet(sfd);
  // Either a DISCONNECT or empty (connection closed)
  if (!raw.empty()) {
    auto pkt = decode_any(raw);
    CHECK(std::holds_alternative<DisconnectPacket>(pkt));
  }

  raw_close(sfd);
  broker.shutdown();
}

TEST_CASE("client_handler_pingreq_receives_pingresp", "[client_handler]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  std::this_thread::sleep_for(10ms);

  SocketHandle sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(sfd != k_invalid_socket);

  REQUIRE(raw_send(sfd, make_connect("test-ping-1")));

  auto connack_raw = raw_recv_packet(sfd);
  REQUIRE(!connack_raw.empty());
  REQUIRE(std::holds_alternative<ConnackPacket>(decode_any(connack_raw)));

  WriteBuffer ping_buf;
  encode_pingreq(ping_buf);
  REQUIRE(raw_send(sfd, ping_buf));

  auto resp_raw = raw_recv_packet(sfd);
  REQUIRE(!resp_raw.empty());
  auto resp = decode_any(resp_raw);
  CHECK(std::holds_alternative<PingrespPacket>(resp));

  raw_close(sfd);
  broker.shutdown();
}

TEST_CASE("client_handler_subscribe_receives_suback", "[client_handler]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  std::this_thread::sleep_for(10ms);

  SocketHandle sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(sfd != k_invalid_socket);

  REQUIRE(raw_send(sfd, make_connect("test-sub-1")));

  auto connack_raw = raw_recv_packet(sfd);
  REQUIRE(!connack_raw.empty());
  REQUIRE(std::holds_alternative<ConnackPacket>(decode_any(connack_raw)));

  // Send SUBSCRIBE
  SubscribePacket sub;
  sub.packet_id = 1U;
  SubscribeFilter flt;
  flt.topic_filter.value = "test/topic";
  flt.options.max_qos = QoS::AtMostOnce;
  sub.filters.push_back(flt);
  WriteBuffer sub_buf;
  encode_subscribe(sub_buf, sub);
  REQUIRE(raw_send(sfd, sub_buf));

  auto suback_raw = raw_recv_packet(sfd);
  REQUIRE(!suback_raw.empty());
  auto suback_pkt = decode_any(suback_raw);
  REQUIRE(std::holds_alternative<SubackPacket>(suback_pkt));
  auto &suback = std::get<SubackPacket>(suback_pkt);
  CHECK(suback.packet_id == 1U);
  REQUIRE(suback.reason_codes.size() == 1U);
  CHECK(suback.reason_codes[0] == ReasonCode::Success);

  raw_close(sfd);
  broker.shutdown();
}

TEST_CASE("client_handler_publish_qos0_no_ack", "[client_handler]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  std::this_thread::sleep_for(10ms);

  SocketHandle sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(sfd != k_invalid_socket);

  REQUIRE(raw_send(sfd, make_connect("test-pub-1")));

  auto connack_raw = raw_recv_packet(sfd);
  REQUIRE(!connack_raw.empty());
  REQUIRE(std::holds_alternative<ConnackPacket>(decode_any(connack_raw)));

  // Send QoS 0 PUBLISH — no response expected
  PublishPacket pub;
  pub.topic.value = "sensor/data";
  pub.qos = QoS::AtMostOnce;
  pub.payload.data = {0x41U, 0x42U};
  WriteBuffer pub_buf;
  encode_publish(pub_buf, pub);
  REQUIRE(raw_send(sfd, pub_buf));

  // Send a PINGREQ to confirm the server is still responsive
  WriteBuffer ping_buf;
  encode_pingreq(ping_buf);
  REQUIRE(raw_send(sfd, ping_buf));

  auto resp_raw = raw_recv_packet(sfd);
  REQUIRE(!resp_raw.empty());
  CHECK(std::holds_alternative<PingrespPacket>(decode_any(resp_raw)));

  raw_close(sfd);
  broker.shutdown();
}

TEST_CASE("client_handler_publish_qos1_receives_puback", "[client_handler]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  std::this_thread::sleep_for(10ms);

  SocketHandle sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(sfd != k_invalid_socket);

  REQUIRE(raw_send(sfd, make_connect("test-pub-qos1")));

  auto connack_raw = raw_recv_packet(sfd);
  REQUIRE(!connack_raw.empty());
  REQUIRE(std::holds_alternative<ConnackPacket>(decode_any(connack_raw)));

  PublishPacket pub;
  pub.topic.value = "sensor/data";
  pub.qos = QoS::AtLeastOnce;
  pub.packet_id = 1U;
  pub.payload.data = {0x41U};
  WriteBuffer pub_buf;
  encode_publish(pub_buf, pub);
  REQUIRE(raw_send(sfd, pub_buf));

  auto puback_raw = raw_recv_packet(sfd);
  REQUIRE(!puback_raw.empty());
  auto puback_pkt = decode_any(puback_raw);
  REQUIRE(std::holds_alternative<PubackPacket>(puback_pkt));
  CHECK(std::get<PubackPacket>(puback_pkt).packet_id == 1U);
  CHECK(std::get<PubackPacket>(puback_pkt).reason_code == ReasonCode::Success);

  raw_close(sfd);
  broker.shutdown();
}
TEST_CASE("client_handler_qos1_outbound_fanout", "[client_handler]") {
  // Subscriber uses QoS1. Publisher sends QoS1.
  // Covers: send_message QoS1 branch, publish_from_message(pkt_id),
  //         outbound PUBLISH with packet_id, and PUBACK handler.
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  std::this_thread::sleep_for(10ms);

  // Subscriber
  SocketHandle sub_sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(sub_sfd != k_invalid_socket);
  REQUIRE(raw_send(sub_sfd, make_connect("qos1-sub")));
  auto sc = raw_recv_packet(sub_sfd);
  REQUIRE(!sc.empty());
  REQUIRE(std::holds_alternative<ConnackPacket>(decode_any(sc)));

  SubscribePacket sub_pkt;
  sub_pkt.packet_id = 1U;
  SubscribeFilter sf;
  sf.topic_filter.value = "qos1/out";
  sf.options.max_qos = QoS::AtLeastOnce;
  sub_pkt.filters.push_back(sf);
  WriteBuffer sb;
  encode_subscribe(sb, sub_pkt);
  REQUIRE(raw_send(sub_sfd, sb));
  auto sa = raw_recv_packet(sub_sfd); // SUBACK
  REQUIRE(!sa.empty());

  // Publisher
  SocketHandle pub_sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(pub_sfd != k_invalid_socket);
  REQUIRE(raw_send(pub_sfd, make_connect("qos1-pub")));
  auto pc = raw_recv_packet(pub_sfd);
  REQUIRE(!pc.empty());

  PublishPacket pub;
  pub.topic.value = "qos1/out";
  pub.qos = QoS::AtLeastOnce;
  pub.packet_id = 1U;
  pub.payload.data = {0xAAU};
  WriteBuffer pb;
  encode_publish(pb, pub);
  REQUIRE(raw_send(pub_sfd, pb));

  // Publisher gets PUBACK for its own publish
  auto pub_ack = raw_recv_packet(pub_sfd);
  REQUIRE(!pub_ack.empty());
  REQUIRE(std::holds_alternative<PubackPacket>(decode_any(pub_ack)));

  // Subscriber receives outbound QoS1 PUBLISH from broker
  auto delivered_raw = raw_recv_packet(sub_sfd);
  REQUIRE(!delivered_raw.empty());
  auto delivered_pkt = decode_any(delivered_raw);
  REQUIRE(std::holds_alternative<PublishPacket>(delivered_pkt));
  auto &delivered = std::get<PublishPacket>(delivered_pkt);
  CHECK(delivered.qos == QoS::AtLeastOnce);
  CHECK(delivered.topic.value == "qos1/out");

  // Subscriber sends PUBACK back to broker (covers PubackPacket handler)
  PubackPacket sub_puback;
  sub_puback.packet_id = delivered.packet_id.value_or(0U);
  sub_puback.reason_code = ReasonCode::Success;
  WriteBuffer sub_pb;
  encode_puback(sub_pb, sub_puback);
  REQUIRE(raw_send(sub_sfd, sub_pb));

  // Confirm connection still live
  WriteBuffer ping;
  encode_pingreq(ping);
  REQUIRE(raw_send(sub_sfd, ping));
  auto pr = raw_recv_packet(sub_sfd);
  REQUIRE(!pr.empty());
  CHECK(std::holds_alternative<PingrespPacket>(decode_any(pr)));

  raw_close(sub_sfd);
  raw_close(pub_sfd);
  broker.shutdown();
}

TEST_CASE("client_handler_qos2_outbound_fanout", "[client_handler]") {
  // Subscriber uses QoS2. Publisher sends QoS2.
  // Covers: send_message QoS2 outbound branch, PubrecPacket handler,
  //         PubcompPacket handler.
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  std::this_thread::sleep_for(10ms);

  // Subscriber
  SocketHandle sub_sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(sub_sfd != k_invalid_socket);
  REQUIRE(raw_send(sub_sfd, make_connect("qos2-out-sub")));
  auto sc = raw_recv_packet(sub_sfd);
  REQUIRE(!sc.empty());
  REQUIRE(std::holds_alternative<ConnackPacket>(decode_any(sc)));

  SubscribePacket sub_pkt;
  sub_pkt.packet_id = 1U;
  SubscribeFilter sf;
  sf.topic_filter.value = "qos2/out";
  sf.options.max_qos = QoS::ExactlyOnce;
  sub_pkt.filters.push_back(sf);
  WriteBuffer sb;
  encode_subscribe(sb, sub_pkt);
  REQUIRE(raw_send(sub_sfd, sb));
  auto sa = raw_recv_packet(sub_sfd); // SUBACK
  REQUIRE(!sa.empty());

  // Publisher
  SocketHandle pub_sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(pub_sfd != k_invalid_socket);
  REQUIRE(raw_send(pub_sfd, make_connect("qos2-out-pub")));
  auto pc = raw_recv_packet(pub_sfd);
  REQUIRE(!pc.empty());

  PublishPacket pub;
  pub.topic.value = "qos2/out";
  pub.qos = QoS::ExactlyOnce;
  pub.packet_id = 1U;
  pub.payload.data = {0xBBU};
  WriteBuffer pb;
  encode_publish(pb, pub);
  REQUIRE(raw_send(pub_sfd, pb));

  // Publisher receives PUBREC
  auto pub_pubrec = raw_recv_packet(pub_sfd);
  REQUIRE(!pub_pubrec.empty());
  REQUIRE(std::holds_alternative<PubrecPacket>(decode_any(pub_pubrec)));

  // Publisher sends PUBREL
  PubrelPacket pubrel;
  pubrel.packet_id = 1U;
  WriteBuffer prl;
  encode_pubrel(prl, pubrel);
  REQUIRE(raw_send(pub_sfd, prl));

  // Publisher receives PUBCOMP
  auto pub_pubcomp = raw_recv_packet(pub_sfd);
  REQUIRE(!pub_pubcomp.empty());
  REQUIRE(std::holds_alternative<PubcompPacket>(decode_any(pub_pubcomp)));

  // Subscriber receives outbound QoS2 PUBLISH
  auto delivered_raw = raw_recv_packet(sub_sfd);
  REQUIRE(!delivered_raw.empty());
  auto delivered_pkt = decode_any(delivered_raw);
  REQUIRE(std::holds_alternative<PublishPacket>(delivered_pkt));
  auto &delivered = std::get<PublishPacket>(delivered_pkt);
  CHECK(delivered.qos == QoS::ExactlyOnce);

  // Subscriber sends PUBREC (covers PubrecPacket handler arm on subscriber
  // conn)
  PubrecPacket sub_pubrec;
  sub_pubrec.packet_id = delivered.packet_id.value_or(0U);
  sub_pubrec.reason_code = ReasonCode::Success;
  WriteBuffer sp;
  encode_pubrec(sp, sub_pubrec);
  REQUIRE(raw_send(sub_sfd, sp));

  // Broker sends PUBREL to subscriber (covers outbound PUBREL from
  // qos2.on_pubrec_received)
  auto broker_pubrel_raw = raw_recv_packet(sub_sfd);
  REQUIRE(!broker_pubrel_raw.empty());
  REQUIRE(std::holds_alternative<PubrelPacket>(decode_any(broker_pubrel_raw)));

  // Subscriber sends PUBCOMP back (covers PubcompPacket handler)
  PubcompPacket sub_pubcomp;
  sub_pubcomp.packet_id = delivered.packet_id.value_or(0U);
  sub_pubcomp.reason_code = ReasonCode::Success;
  WriteBuffer sc2;
  encode_pubcomp(sc2, sub_pubcomp);
  REQUIRE(raw_send(sub_sfd, sc2));

  // Confirm still live
  WriteBuffer ping;
  encode_pingreq(ping);
  REQUIRE(raw_send(sub_sfd, ping));
  auto pr = raw_recv_packet(sub_sfd);
  REQUIRE(!pr.empty());
  CHECK(std::holds_alternative<PingrespPacket>(decode_any(pr)));

  raw_close(sub_sfd);
  raw_close(pub_sfd);
  broker.shutdown();
}
TEST_CASE("client_handler_publish_qos2_full_handshake", "[client_handler]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  std::this_thread::sleep_for(10ms);

  SocketHandle sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(sfd != k_invalid_socket);

  REQUIRE(raw_send(sfd, make_connect("test-pub-qos2")));

  auto connack_raw = raw_recv_packet(sfd);
  REQUIRE(!connack_raw.empty());
  REQUIRE(std::holds_alternative<ConnackPacket>(decode_any(connack_raw)));

  // PUBLISH QoS 2
  PublishPacket pub;
  pub.topic.value = "data/exact";
  pub.qos = QoS::ExactlyOnce;
  pub.packet_id = 1U;
  pub.payload.data = {0x01U};
  WriteBuffer p1;
  encode_publish(p1, pub);
  REQUIRE(raw_send(sfd, p1));

  // Receive PUBREC
  auto pubrec_raw = raw_recv_packet(sfd);
  REQUIRE(!pubrec_raw.empty());
  auto pubrec_pkt = decode_any(pubrec_raw);
  REQUIRE(std::holds_alternative<PubrecPacket>(pubrec_pkt));
  CHECK(std::get<PubrecPacket>(pubrec_pkt).packet_id == 1U);

  // Send PUBREL
  PubrelPacket pubrel;
  pubrel.packet_id = 1U;
  WriteBuffer p2;
  encode_pubrel(p2, pubrel);
  REQUIRE(raw_send(sfd, p2));

  // Receive PUBCOMP
  auto pubcomp_raw = raw_recv_packet(sfd);
  REQUIRE(!pubcomp_raw.empty());
  auto pubcomp_pkt = decode_any(pubcomp_raw);
  REQUIRE(std::holds_alternative<PubcompPacket>(pubcomp_pkt));
  CHECK(std::get<PubcompPacket>(pubcomp_pkt).packet_id == 1U);

  raw_close(sfd);
  broker.shutdown();
}

TEST_CASE("client_handler_unsubscribe_receives_unsuback", "[client_handler]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  std::this_thread::sleep_for(10ms);

  SocketHandle sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(sfd != k_invalid_socket);

  REQUIRE(raw_send(sfd, make_connect("test-unsub-1")));

  auto connack_raw = raw_recv_packet(sfd);
  REQUIRE(!connack_raw.empty());
  REQUIRE(std::holds_alternative<ConnackPacket>(decode_any(connack_raw)));

  UnsubscribePacket unsub;
  unsub.packet_id = 5U;
  Utf8String filter;
  filter.value = "test/topic";
  unsub.topic_filters.push_back(filter);
  WriteBuffer unsub_buf;
  encode_unsubscribe(unsub_buf, unsub);
  REQUIRE(raw_send(sfd, unsub_buf));

  auto unsuback_raw = raw_recv_packet(sfd);
  REQUIRE(!unsuback_raw.empty());
  auto unsuback_pkt = decode_any(unsuback_raw);
  REQUIRE(std::holds_alternative<UnsubackPacket>(unsuback_pkt));
  CHECK(std::get<UnsubackPacket>(unsuback_pkt).packet_id == 5U);

  raw_close(sfd);
  broker.shutdown();
}

TEST_CASE("client_handler_clean_disconnect", "[client_handler]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  std::this_thread::sleep_for(10ms);

  SocketHandle sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(sfd != k_invalid_socket);

  REQUIRE(raw_send(sfd, make_connect("test-dis-1")));

  auto connack_raw = raw_recv_packet(sfd);
  REQUIRE(!connack_raw.empty());
  REQUIRE(std::holds_alternative<ConnackPacket>(decode_any(connack_raw)));

  // Send DISCONNECT
  DisconnectPacket dis;
  dis.reason_code = ReasonCode::Success;
  WriteBuffer dis_buf;
  encode_disconnect(dis_buf, dis);
  REQUIRE(raw_send(sfd, dis_buf));

  // Give handler time to process then close
  std::this_thread::sleep_for(50ms);
  raw_close(sfd);
  broker.shutdown();
}

TEST_CASE("client_handler_subscribe_publish_fanout", "[client_handler]") {
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  std::this_thread::sleep_for(10ms);

  // Subscriber
  SocketHandle sub_sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(sub_sfd != k_invalid_socket);
  REQUIRE(raw_send(sub_sfd, make_connect("fanout-sub")));
  auto sub_connack = raw_recv_packet(sub_sfd);
  REQUIRE(!sub_connack.empty());

  SubscribePacket sub_pkt;
  sub_pkt.packet_id = 1U;
  SubscribeFilter sflt;
  sflt.topic_filter.value = "fanout/test";
  sflt.options.max_qos = QoS::AtMostOnce;
  sub_pkt.filters.push_back(sflt);
  WriteBuffer sub_buf;
  encode_subscribe(sub_buf, sub_pkt);
  REQUIRE(raw_send(sub_sfd, sub_buf));

  auto suback = raw_recv_packet(sub_sfd);
  REQUIRE(!suback.empty());
  REQUIRE(std::holds_alternative<SubackPacket>(decode_any(suback)));

  // Publisher (separate connection)
  SocketHandle pub_sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(pub_sfd != k_invalid_socket);
  REQUIRE(raw_send(pub_sfd, make_connect("fanout-pub")));
  auto pub_connack = raw_recv_packet(pub_sfd);
  REQUIRE(!pub_connack.empty());

  PublishPacket pub;
  pub.topic.value = "fanout/test";
  pub.qos = QoS::AtMostOnce;
  pub.payload.data = {0xBEU, 0xEFU};
  WriteBuffer pub_buf;
  encode_publish(pub_buf, pub);
  REQUIRE(raw_send(pub_sfd, pub_buf));

  // Subscriber should receive the message
  auto msg_raw = raw_recv_packet(sub_sfd);
  REQUIRE(!msg_raw.empty());
  auto msg_pkt = decode_any(msg_raw);
  REQUIRE(std::holds_alternative<PublishPacket>(msg_pkt));
  auto &delivered = std::get<PublishPacket>(msg_pkt);
  CHECK(delivered.topic.value == "fanout/test");
  CHECK(delivered.payload.data == std::vector<uint8_t>{0xBEU, 0xEFU});

  raw_close(sub_sfd);
  raw_close(pub_sfd);
  broker.shutdown();
}

TEST_CASE("client_handler_disconnect_with_session_expiry_override",
          "[client_handler]") {
  // Covers extract_expiry_override when SessionExpiryInterval IS present.
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  std::this_thread::sleep_for(10ms);

  SocketHandle sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(sfd != k_invalid_socket);

  REQUIRE(raw_send(sfd, make_connect("test-expiry-1")));

  auto connack_raw = raw_recv_packet(sfd);
  REQUIRE(!connack_raw.empty());
  REQUIRE(std::holds_alternative<ConnackPacket>(decode_any(connack_raw)));

  // DISCONNECT with SessionExpiryInterval = 60 s
  DisconnectPacket dis;
  dis.reason_code = ReasonCode::Success;
  Property expiry_prop;
  expiry_prop.id = PropertyId::SessionExpiryInterval;
  expiry_prop.value = static_cast<uint32_t>(60U);
  dis.properties.push_back(expiry_prop);
  WriteBuffer dis_buf;
  encode_disconnect(dis_buf, dis);
  REQUIRE(raw_send(sfd, dis_buf));

  std::this_thread::sleep_for(50ms);
  raw_close(sfd);
  broker.shutdown();
}

TEST_CASE("client_handler_connect_with_receive_maximum_property",
          "[client_handler]") {
  // Covers extract_receive_maximum when ReceiveMaximum IS in CONNECT props.
  BrokerConfig cfg = make_test_config();
  Broker broker(cfg);
  broker.startup();

  std::this_thread::sleep_for(10ms);

  SocketHandle sfd = raw_connect(k_test_mqtt_port);
  REQUIRE(sfd != k_invalid_socket);

  ConnectPacket connect_pkt;
  connect_pkt.client_id.value = "test-recv-max";
  connect_pkt.clean_start = true;
  connect_pkt.keep_alive = 0U;
  Property rm_prop;
  rm_prop.id = PropertyId::ReceiveMaximum;
  rm_prop.value = static_cast<uint16_t>(10U);
  connect_pkt.properties.push_back(rm_prop);
  WriteBuffer con_buf;
  encode_connect(con_buf, connect_pkt);
  REQUIRE(raw_send(sfd, con_buf));

  auto connack_raw = raw_recv_packet(sfd);
  REQUIRE(!connack_raw.empty());
  auto connack_pkt = decode_any(connack_raw);
  REQUIRE(std::holds_alternative<ConnackPacket>(connack_pkt));
  CHECK(std::get<ConnackPacket>(connack_pkt).reason_code ==
        ReasonCode::Success);

  raw_close(sfd);
  broker.shutdown();
}
