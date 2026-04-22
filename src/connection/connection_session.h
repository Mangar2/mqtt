#pragma once

/**
 * @file connection_session.h
 * @brief Per-connection heap-owned session state for reactor/worker processing.
 */

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "broker/broker_config.h"
#include "broker/connect_result.h"
#include "codec/write_buffer.h"
#include "connection/connection_flow_support.h"
#include "connection/receive_maximum.h"
#include "connection/topic_alias_table.h"
#include "data_model/packet/connect_packet.h"
#include "network/stream_buffer.h"
#include "network/tcp_connection.h"
#include "transport/websocket_transport.h"

namespace mqtt {

class ClientSession;
class TcpConnection;
class WebSocketTransport;
class OutboundQueue;

/**
 * @brief Heap-owned per-connection state object.
 *
 * Thread safety: none.
 *
 * Access to one connection is serialized by the job scheduler contract:
 * for one socket fd, at most one worker executes connection code at a time.
 * Different fds may run concurrently on different worker threads.
 */
class ConnectionSession {
public:
  enum class Phase {
    Handshake,
    Connected,
    Closing,
  };

  ConnectionSession(std::unique_ptr<TcpConnection> connection,
                    std::unique_ptr<WebSocketTransport> ws_transport,
                    bool is_websocket, const BrokerConfig &config);
  ~ConnectionSession();

  TcpConnection &connection() noexcept;
  WebSocketTransport *ws_transport() noexcept;
  [[nodiscard]] bool is_websocket() const noexcept;
  StreamBuffer &stream_buffer() noexcept;
  TopicAliasTable &topic_alias_table() noexcept;
  ReceiveMaximum &inbound_receive_window() noexcept;

  void install_client_session(std::unique_ptr<ClientSession> client_session);
  ClientSession *client_session() noexcept;
  std::shared_ptr<OutboundQueue> outbound_queue() noexcept;

  [[nodiscard]] Phase phase() const noexcept;
  void set_phase(Phase phase) noexcept;

  RuntimeDisconnectState &disconnect_state() noexcept;

  std::optional<ConnectPacket> &connect_packet() noexcept;
  ConnectResult &connect_result() noexcept;

  std::vector<WriteBuffer> &pending_write_frames() noexcept;
  void clear_pending_write_frames() noexcept;

  void request_session_takeover() noexcept;
  [[nodiscard]] bool consume_session_takeover_request() noexcept;

private:
  std::unique_ptr<TcpConnection> connection_;
  std::unique_ptr<WebSocketTransport> ws_transport_;
  bool is_websocket_{false};
  StreamBuffer stream_buffer_{};
  TopicAliasTable topic_alias_table_;
  ReceiveMaximum inbound_receive_window_;
  std::unique_ptr<ClientSession> client_session_;
  Phase phase_{Phase::Handshake};
  RuntimeDisconnectState disconnect_state_{};
  std::optional<ConnectPacket> connect_packet_;
  ConnectResult connect_result_{};
  std::vector<WriteBuffer> pending_write_frames_;
  std::atomic<bool> session_takeover_requested_{false};
};

} // namespace mqtt
