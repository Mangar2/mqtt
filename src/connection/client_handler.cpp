/**
 * @file client_handler.cpp
 * @brief ClientHandler implementation — MQTT 5.0 per-client session loop
 *        (Module 17).
 */

#include "connection/client_handler.h"

#include <array>
#include <chrono>
#include <thread>
#include <variant>
#include <vector>

#include "auth/enhanced_auth_handler.h"
#include "broker/broker.h"
#include "codec/packet/connect_codec.h"
#include "codec/packet/control_codec.h"
#include "codec/packet/publish_codec.h"
#include "codec/packet/subscribe_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "codec/read_buffer.h"
#include "codec/write_buffer.h"
#include "connection/connection_state.h"
#include "connection/keep_alive_timer.h"
#include "connection/receive_maximum.h"
#include "connection/topic_alias_table.h"
#include "data_model/packet/connect_packet.h"
#include "data_model/packet/control_packets.h"
#include "data_model/packet/publish_packets.h"
#include "data_model/packet/subscribe_packets.h"
#include "data_model/property/property.h"
#include "data_model/property/property_id.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/subscription/subscription.h"
#include "network/stream_buffer.h"
#include "network/write_queue.h"
#include "qos/packet_id_manager.h"
#include "qos/qos1_state_machine.h"
#include "qos/qos2_state_machine.h"
#include "session_manager/session_manager.h"
#include "transport/websocket_transport.h"
#include "will_manager/will_publisher.h"

namespace mqtt {

namespace {

// ── Constants ────────────────────────────────────────────────────────────────

/// Receive-timeout interval in milliseconds. Used for keep-alive polling.
constexpr uint32_t k_recv_timeout_ms = 500U;

/// Read chunk size in bytes.
constexpr std::size_t k_read_chunk = 4096U;

// ── Helpers ──────────────────────────────────────────────────────────────────

/**
 * @brief Extract the session_expiry_interval from DISCONNECT properties.
 * @return Override value if present, else std::nullopt.
 */
[[nodiscard]] std::optional<uint32_t>
extract_expiry_override(const std::vector<Property> &props) {
  for (const auto &prop : props) {
    if (prop.id == PropertyId::SessionExpiryInterval) {
      return std::get<uint32_t>(prop.value);
    }
  }
  return std::nullopt;
}

/**
 * @brief Extract the client-advertised Receive Maximum from CONNECT properties.
 * @return Value in [1, 65535], or 65535 if the property is absent.
 */
[[nodiscard]] uint16_t
extract_receive_maximum(const std::vector<Property> &props) {
  for (const auto &prop : props) {
    if (prop.id == PropertyId::ReceiveMaximum) {
      return std::get<uint16_t>(prop.value);
    }
  }
  return 65535U;
}

/**
 * @brief Encode a packet and enqueue it on the WriteQueue.
 */
template <typename EncodeFn> void send(WriteQueue &queue, EncodeFn encode_fn) {
  WriteBuffer buf;
  encode_fn(buf);
  (void)queue.enqueue(std::move(buf));
}

/**
 * @brief Build a Message from a PublishPacket.
 */
[[nodiscard]] Message message_from_publish(const PublishPacket &pkt) {
  return Message{
      .topic = pkt.topic,
      .payload = pkt.payload,
      .qos = pkt.qos,
      .retain = pkt.retain,
      .properties = pkt.properties,
  };
}

/**
 * @brief Build a PublishPacket from a Message for outbound delivery.
 */
[[nodiscard]] PublishPacket publish_from_message(const Message &msg,
                                                 uint16_t pkt_id = 0U) {
  PublishPacket pkt;
  pkt.topic = msg.topic;
  pkt.payload = msg.payload;
  pkt.qos = msg.qos;
  pkt.retain = msg.retain;
  pkt.properties = msg.properties;
  if (msg.qos != QoS::AtMostOnce) {
    pkt.packet_id = pkt_id;
  }
  return pkt;
}

/**
 * @brief Bytes read from the transport, with a flag indicating timeout.
 */
struct ReadResult {
  std::vector<uint8_t> data;
  bool timed_out{false};
  bool eof{false};
};

/**
 * @brief Read a chunk from a plain TCP connection.
 */
[[nodiscard]] ReadResult read_chunk_tcp(TcpConnection &conn) {
  std::array<uint8_t, k_read_chunk> raw{};
  std::ptrdiff_t num = conn.read(raw);
  if (num <= 0) {
    return {
        .data = {}, .timed_out = conn.last_read_timed_out(), .eof = (num == 0)};
  }
  ReadResult res;
  res.data.assign(raw.begin(), raw.begin() + num);
  return res;
};

[[nodiscard]] ReadResult read_chunk_ws(WebSocketTransport &transport) {
  WsReadChunk ws_chunk = transport.read_chunk();
  return {
      .data = std::move(ws_chunk.data),
      .timed_out = ws_chunk.timed_out,
      .eof = ws_chunk.eof,
  };
}

} // namespace

// ── ClientHandler::run
// ────────────────────────────────────────────────────────

void ClientHandler::run(std::unique_ptr<TcpConnection> conn, Broker &broker,
                        const BrokerConfig &config, bool is_ws) {
  // 17.1.2 — WebSocket upgrade
  std::optional<WebSocketTransport> ws_transport;
  if (is_ws) {
    try {
      ws_transport.emplace(*conn);
    } catch (...) {
      return;
    }
  }

  // 17.1.3 — set recv timeout for keep-alive polling
  if (ws_transport.has_value()) {
    ws_transport->set_receive_timeout(k_recv_timeout_ms);
  } else {
    conn->set_receive_timeout(k_recv_timeout_ms);
  }

  // Per-connection state
  StreamBuffer stream_buf;
  WriteQueue write_queue;

  // --- 17.2 CONNECT handshake ---

  // Read the first complete packet
  auto read_first_packet = [&]() -> std::optional<std::vector<uint8_t>> {
    // Allow several seconds for the first packet before giving up.
    // Keep-alive not yet negotiated; use a hard limit of ~10 s.
    for (int tries = 0; tries < 20; ++tries) {
      ReadResult res = ws_transport.has_value() ? read_chunk_ws(*ws_transport)
                                                : read_chunk_tcp(*conn);
      if (res.eof) {
        return std::nullopt;
      }
      if (!res.timed_out && !res.data.empty()) {
        stream_buf.append(res.data);
      }
      if (stream_buf.has_complete_packet()) {
        return stream_buf.consume_packet();
      }
    }
    return std::nullopt;
  };

  // Helper to enqueue encoded bytes through the right transport
  auto enqueue_bytes = [&](WriteBuffer pkt_bytes) {
    if (ws_transport.has_value()) {
      auto framed = WebSocketTransport::encode_frame(pkt_bytes);
      (void)write_queue.enqueue(std::move(framed));
    } else {
      (void)write_queue.enqueue(std::move(pkt_bytes));
    }
  };

  // Read the first MQTT packet
  std::optional<std::vector<uint8_t>> first_pkt_bytes = read_first_packet();

  if (!first_pkt_bytes.has_value()) {
    return;
  }

  // 17.2.1 — decode first packet, must be CONNECT
  ConnectPacket connect;
  try {
    auto rbuf = ReadBuffer{std::span<const uint8_t>(*first_pkt_bytes)};
    auto pkt = read_packet(rbuf);
    if (!std::holds_alternative<ConnectPacket>(pkt)) {
      // Not a CONNECT — send DISCONNECT with protocol error and close
      WriteBuffer dis_buf;
      DisconnectPacket dis_pkt;
      dis_pkt.reason_code = ReasonCode::ProtocolError;
      encode_disconnect(dis_buf, dis_pkt);
      (void)conn->write(dis_buf);
      return;
    }
    connect = std::get<ConnectPacket>(std::move(pkt));
  } catch (...) {
    return;
  }

  // 17.2.2 — authenticate
  EnhancedAuthHandler auth_handler(std::shared_ptr<IAuthenticator>(
      &broker.authenticator(), [](IAuthenticator *) {}));

  AuthResult auth_result = auth_handler.initiate(connect);

  if (auth_result.status == AuthStatus::Failure) {
    WriteBuffer buf;
    ConnackPacket nak;
    nak.session_present = false;
    nak.reason_code = auth_result.reason_code;
    encode_connack(buf, nak);
    enqueue_bytes(std::move(buf));
    (void)write_queue.drain(*conn);
    return;
  }

  // Multi-step auth (AUTH packets) — loop until complete or failure
  while (auth_result.status == AuthStatus::Continue) {
    // Send AUTH(ContinueAuthentication) to client
    AuthPacket auth_pkt;
    auth_pkt.reason_code = ReasonCode::ContinueAuthentication;
    if (auth_result.auth_data.has_value()) {
      Property auth_data_prop;
      auth_data_prop.id = PropertyId::AuthenticationData;
      auth_data_prop.value = *auth_result.auth_data;
      auth_pkt.properties.push_back(auth_data_prop);
    }
    WriteBuffer auth_buf;
    encode_auth(auth_buf, auth_pkt);
    enqueue_bytes(std::move(auth_buf));
    (void)write_queue.drain(*conn);

    // Wait for client AUTH packet
    bool got_response = false;
    for (int tries = 0; tries < 20 && !got_response; ++tries) {
      ReadResult res = ws_transport.has_value() ? read_chunk_ws(*ws_transport)
                                                : read_chunk_tcp(*conn);
      if (res.eof) {
        return;
      }
      if (!res.timed_out && !res.data.empty()) {
        stream_buf.append(res.data);
      }
      if (stream_buf.has_complete_packet()) {
        auto raw = stream_buf.consume_packet();
        try {
          auto rbuf = ReadBuffer{std::span<const uint8_t>(raw)};
          auto pkt = read_packet(rbuf);
          if (!std::holds_alternative<AuthPacket>(pkt)) {
            return;
          }
          auth_result = auth_handler.on_auth(std::get<AuthPacket>(pkt));
          got_response = true;
        } catch (...) {
          return;
        }
      }
    }
    if (!got_response) {
      return;
    }
  }

  if (auth_result.status != AuthStatus::Success) {
    WriteBuffer buf;
    ConnackPacket nak2;
    nak2.session_present = false;
    nak2.reason_code = auth_result.reason_code;
    encode_connack(buf, nak2);
    enqueue_bytes(std::move(buf));
    (void)write_queue.drain(*conn);
    return;
  }

  // 17.2.3 — open / resume session
  std::string client_id = connect.client_id.value;
  SessionOpenResult session_result;
  try {
    session_result = broker.session_manager().handle_connect(
        connect, [&conn]() { conn->close(); });
  } catch (...) {
    WriteBuffer buf;
    ConnackPacket nak3;
    nak3.reason_code = ReasonCode::ClientIdentifierNotValid;
    encode_connack(buf, nak3);
    enqueue_bytes(std::move(buf));
    (void)write_queue.drain(*conn);
    return;
  }

  // 11.1.1 — store will if present
  if (connect.will.has_value()) {
    WillMessage will_msg;
    will_msg.message.topic = connect.will->topic;
    will_msg.message.payload = connect.will->payload;
    will_msg.message.qos = connect.will->qos;
    will_msg.message.retain = connect.will->retain;
    will_msg.message.properties = connect.will->properties;
    // Extract Will Delay Interval
    for (const auto &prop : connect.will->properties) {
      if (prop.id == PropertyId::WillDelayInterval) {
        will_msg.delay_interval = std::get<uint32_t>(prop.value);
        break;
      }
    }
    broker.will_publisher().on_connect(client_id, will_msg);
  }

  // 17.2.4 — send CONNACK
  {
    ConnackPacket connack;
    connack.session_present = session_result.session_present;
    connack.reason_code = ReasonCode::Success;
    // Advertise server-side Receive Maximum and Topic Alias Maximum
    if (config.receive_maximum != 65535U) {
      Property recv_prop;
      recv_prop.id = PropertyId::ReceiveMaximum;
      recv_prop.value = static_cast<uint16_t>(config.receive_maximum);
      connack.properties.push_back(recv_prop);
    }
    if (config.topic_alias_maximum != 0U) {
      Property alias_prop;
      alias_prop.id = PropertyId::TopicAliasMaximum;
      alias_prop.value = static_cast<uint16_t>(config.topic_alias_maximum);
      connack.properties.push_back(alias_prop);
    }
    WriteBuffer buf;
    encode_connack(buf, connack);
    enqueue_bytes(std::move(buf));
    (void)write_queue.drain(*conn);
  }

  // Per-session objects
  uint16_t client_recv_max = extract_receive_maximum(connect.properties);
  ReceiveMaximum recv_max(client_recv_max);
  TopicAliasTable alias_table(config.topic_alias_maximum);
  KeepAliveTimer ka_timer(connect.keep_alive);
  PacketIdManager id_mgr;
  Qos1StateMachine qos1(client_id, id_mgr, broker.inflight_store());
  Qos2StateMachine qos2(client_id, id_mgr, broker.inflight_store());
  ConnectionStateMachine state_machine;
  state_machine.on_connect();

  // 17.4.1 — register send callback with broker
  auto send_message = [&](const Message &msg) {
    if (msg.qos == QoS::AtMostOnce) {
      WriteBuffer buf;
      encode_publish(buf, publish_from_message(msg));
      enqueue_bytes(std::move(buf));
    } else if (msg.qos == QoS::AtLeastOnce) {
      if (!recv_max.acquire()) {
        return;
      }
      auto pkt = qos1.initiate_publish(msg);
      WriteBuffer buf;
      encode_publish(buf, pkt);
      enqueue_bytes(std::move(buf));
    } else {
      if (!recv_max.acquire()) {
        return;
      }
      auto pkt = qos2.initiate_publish(msg);
      WriteBuffer buf;
      encode_publish(buf, pkt);
      enqueue_bytes(std::move(buf));
    }
  };
  broker.register_connection(client_id, send_message);

  // 17.4.4 — start dedicated drain thread
  std::jthread drain_thread([&](const std::stop_token &stop_tok) {
    (void)stop_tok;
    if (ws_transport.has_value()) {
      write_queue.run_drain(ws_transport->tcp());
    } else {
      write_queue.run_drain(*conn);
    }
  });

  // 17.5.3 — flush offline queue for resumed sessions
  if (session_result.session_present) {
    broker.message_router().flush_offline_queue(client_id);
  }

  // ── 17.3 Per-packet dispatch loop ────────────────────────────────────────
  bool running = true;
  bool clean_disconnect = false;
  ReasonCode disconnect_reason = ReasonCode::Success;
  std::optional<uint32_t> disconnect_expiry_override;

  while (running && broker.is_running()) {
    // Read a chunk from the socket
    ReadResult res = ws_transport.has_value() ? read_chunk_ws(*ws_transport)
                                              : read_chunk_tcp(*conn);

    if (res.eof) {
      running = false;
      break;
    }

    if (!res.timed_out && !res.data.empty()) {
      stream_buf.append(res.data);
    }

    // 17.3.2 — reset keep-alive on received data
    if (!res.timed_out && !res.data.empty()) {
      ka_timer.reset();
    }

    // 17.5.1 — check keep-alive expiry
    if (ka_timer.is_expired()) {
      WriteBuffer buf;
      DisconnectPacket ka_dis;
      ka_dis.reason_code = ReasonCode::KeepAliveTimeout;
      encode_disconnect(buf, ka_dis);
      enqueue_bytes(std::move(buf));
      running = false;
      break;
    }

    // Dispatch all complete packets
    while (stream_buf.has_complete_packet()) {
      auto pkt_bytes = stream_buf.consume_packet();
      AnyPacket pkt;
      try {
        auto rbuf = ReadBuffer{std::span<const uint8_t>(pkt_bytes)};
        pkt = read_packet(rbuf);
      } catch (...) {
        WriteBuffer buf;
        DisconnectPacket mal_dis;
        mal_dis.reason_code = ReasonCode::MalformedPacket;
        encode_disconnect(buf, mal_dis);
        enqueue_bytes(std::move(buf));
        running = false;
        break;
      }

      std::visit(
          [&](auto &&packet) {
            using T = std::decay_t<decltype(packet)>;

            if constexpr (std::is_same_v<T, PublishPacket>) {
              // 17.3.3
              Message msg = message_from_publish(packet);
              if (packet.qos == QoS::AtMostOnce) {
                try {
                  broker.route_message(msg, client_id,
                                       connect.username.has_value()
                                           ? connect.username->value
                                           : "",
                                       alias_table);
                } catch (...) {
                }
              } else if (packet.qos == QoS::AtLeastOnce) {
                auto puback = Qos1StateMachine::on_publish_received(packet);
                WriteBuffer buf;
                encode_puback(buf, puback);
                enqueue_bytes(std::move(buf));
                if (!packet.dup) {
                  try {
                    broker.route_message(msg, client_id,
                                         connect.username.has_value()
                                             ? connect.username->value
                                             : "",
                                         alias_table);
                  } catch (...) {
                  }
                }
              } else {
                auto res2 = qos2.on_publish_received(packet);
                WriteBuffer buf;
                encode_pubrec(buf, res2.pubrec);
                enqueue_bytes(std::move(buf));
                if (!res2.is_duplicate) {
                  try {
                    broker.route_message(msg, client_id,
                                         connect.username.has_value()
                                             ? connect.username->value
                                             : "",
                                         alias_table);
                  } catch (...) {
                  }
                }
              }
            } else if constexpr (std::is_same_v<T, PubackPacket>) {
              // 17.3.6 QoS1
              try {
                qos1.on_puback_received(packet);
                recv_max.release();
              } catch (...) {
              }
            } else if constexpr (std::is_same_v<T, PubrecPacket>) {
              // 17.3.6 QoS2
              try {
                auto pubrel = qos2.on_pubrec_received(packet);
                WriteBuffer buf;
                encode_pubrel(buf, pubrel);
                enqueue_bytes(std::move(buf));
              } catch (...) {
              }
            } else if constexpr (std::is_same_v<T, PubrelPacket>) {
              // 17.3.6 QoS2 inbound
              try {
                auto pubcomp = qos2.on_pubrel_received(packet);
                WriteBuffer buf;
                encode_pubcomp(buf, pubcomp);
                enqueue_bytes(std::move(buf));
              } catch (...) {
              }
            } else if constexpr (std::is_same_v<T, PubcompPacket>) {
              // 17.3.6 QoS2 outbound
              try {
                qos2.on_pubcomp_received(packet);
                recv_max.release();
              } catch (...) {
              }
            } else if constexpr (std::is_same_v<T, SubscribePacket>) {
              // 17.3.4
              SubackPacket suback;
              suback.packet_id = packet.packet_id;
              for (const auto &filter : packet.filters) {
                Subscription sub;
                sub.topic_filter = filter.topic_filter;
                sub.qos = filter.options.max_qos;
                sub.options.no_local = filter.options.no_local;
                sub.options.retain_as_published =
                    filter.options.retain_as_published;
                broker.subscription_store().store(client_id, sub);
                suback.reason_codes.push_back(static_cast<ReasonCode>(
                    static_cast<uint8_t>(filter.options.max_qos)));
                // Deliver retained messages (retain_handling 0 or 1 if new)
                auto retained = broker.retained_message_store().find(
                    filter.topic_filter.value);
                for (auto &rmsg : retained) {
                  send_message(rmsg);
                }
              }
              WriteBuffer buf;
              encode_suback(buf, suback);
              enqueue_bytes(std::move(buf));
            } else if constexpr (std::is_same_v<T, UnsubscribePacket>) {
              // 17.3.5
              UnsubackPacket unsuback;
              unsuback.packet_id = packet.packet_id;
              for (const auto &filter : packet.topic_filters) {
                broker.subscription_store().remove(client_id, filter.value);
                unsuback.reason_codes.push_back(ReasonCode::Success);
              }
              WriteBuffer buf;
              encode_unsuback(buf, unsuback);
              enqueue_bytes(std::move(buf));
            } else if constexpr (std::is_same_v<T, PingreqPacket>) {
              // 17.3.7
              WriteBuffer buf;
              encode_pingresp(buf);
              enqueue_bytes(std::move(buf));
            } else if constexpr (std::is_same_v<T, DisconnectPacket>) {
              // 17.3.8
              clean_disconnect = true;
              disconnect_reason = packet.reason_code;
              disconnect_expiry_override =
                  extract_expiry_override(packet.properties);
              running = false;
            } else if constexpr (std::is_same_v<T, AuthPacket>) {
              // 17.3.9 re-auth
              try {
                auto rres = auth_handler.reauthenticate(packet);
                if (rres.status == AuthStatus::Success) {
                  AuthPacket resp;
                  resp.reason_code = ReasonCode::Success;
                  WriteBuffer buf;
                  encode_auth(buf, resp);
                  enqueue_bytes(std::move(buf));
                } else if (rres.status == AuthStatus::Continue) {
                  AuthPacket resp;
                  resp.reason_code = ReasonCode::ContinueAuthentication;
                  WriteBuffer buf;
                  encode_auth(buf, resp);
                  enqueue_bytes(std::move(buf));
                } else {
                  WriteBuffer buf;
                  DisconnectPacket na_dis;
                  na_dis.reason_code = ReasonCode::NotAuthorized;
                  encode_disconnect(buf, na_dis);
                  enqueue_bytes(std::move(buf));
                  running = false;
                }
              } catch (...) {
                running = false;
              }
            }
            // ConnectPacket / ConnackPacket / SubackPacket etc. at this
            // point in the session are protocol errors — ignore.
          },
          pkt);
    }
  }

  // ── 17.5 Teardown ─────────────────────────────────────────────────────────

  write_queue.stop();
  // drain_thread joins automatically via jthread destructor

  // 17.5.2 / 17.5.4 — will and connection unregistration
  auto now = std::chrono::steady_clock::now();
  if (clean_disconnect) {
    broker.will_publisher().on_disconnect(client_id, disconnect_reason, now);
  } else {
    broker.will_publisher().on_connection_lost(client_id, now);
  }
  broker.unregister_connection(client_id);

  // Session disconnect
  broker.session_manager().handle_disconnect(client_id,
                                             disconnect_expiry_override, now);
}

} // namespace mqtt
