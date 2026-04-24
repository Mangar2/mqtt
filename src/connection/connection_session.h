#pragma once

/**
 * @file connection_session.h
 * @brief Per-connection heap-owned session state for reactor/worker processing.
 */

#include <atomic>
#include <chrono>
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

/**
 * @brief Forward declaration of ClientSession.
 */
class ClientSession;
/**
 * @brief Forward declaration of TcpConnection.
 */
class TcpConnection;
/**
 * @brief Forward declaration of WebSocketTransport.
 */
class WebSocketTransport;
/**
 * @brief Forward declaration of OutboundQueue.
 */
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
  /**
   * @brief Connection processing phase.
   */
  enum class Phase {
    Handshake,
    Connected,
    Closing,
  };

  ConnectionSession(std::unique_ptr<TcpConnection> connection,
                    std::unique_ptr<WebSocketTransport> ws_transport,
                    bool is_websocket, const BrokerConfig &config);
  /**
   * @brief Destroy connection session.
   */
  ~ConnectionSession();

  /**
   * @brief Return underlying TCP connection object.
   * @return TCP connection reference.
   */
  TcpConnection &connection() noexcept;
  /**
   * @brief Return WebSocket transport pointer.
   * @return WebSocket transport pointer or nullptr.
   */
  WebSocketTransport *ws_transport() noexcept;
  /**
   * @brief Return whether this session uses WebSocket transport.
   * @return True when websocket transport is active.
   */
  [[nodiscard]] bool is_websocket() const noexcept;
  /**
   * @brief Return stream buffer used for packet assembly.
   * @return StreamBuffer reference.
   */
  StreamBuffer &stream_buffer() noexcept;
  /**
   * @brief Return per-session topic alias table.
   * @return TopicAliasTable reference.
   */
  TopicAliasTable &topic_alias_table() noexcept;
  /**
   * @brief Return inbound receive-maximum controller.
   * @return ReceiveMaximum reference.
   */
  ReceiveMaximum &inbound_receive_window() noexcept;

  /**
   * @brief Install per-client session context.
   * @param client_session New client session object.
   */
  void install_client_session(std::unique_ptr<ClientSession> client_session);
  /**
   * @brief Return mutable client-session pointer.
   * @return ClientSession pointer or nullptr.
   */
  ClientSession *client_session() noexcept;
  /**
   * @brief Return const client-session pointer.
   * @return Const ClientSession pointer or nullptr.
   */
  [[nodiscard]] const ClientSession *client_session() const noexcept;
  /**
   * @brief Return shared outbound queue used for this session.
   * @return Shared outbound queue pointer.
   */
  std::shared_ptr<OutboundQueue> outbound_queue() noexcept;

  /**
   * @brief Return current phase.
   * @return Current session phase.
   */
  [[nodiscard]] Phase phase() const noexcept;
  /**
   * @brief Set current phase.
   * @param phase New phase value.
   */
  void set_phase(Phase phase) noexcept;

  /**
   * @brief Return disconnect state container.
   * @return RuntimeDisconnectState reference.
   */
  RuntimeDisconnectState &disconnect_state() noexcept;

  /**
   * @brief Return optional cached CONNECT packet.
   * @return Optional CONNECT packet reference.
   */
  std::optional<ConnectPacket> &connect_packet() noexcept;
  /**
   * @brief Return mutable CONNECT handling result.
   * @return ConnectResult reference.
   */
  ConnectResult &connect_result() noexcept;

  /**
   * @brief Return pending write frame container.
   * @return Pending frame vector reference.
   */
  std::vector<WriteBuffer> &pending_write_frames() noexcept;
  /**
   * @brief Clear buffered pending write frames.
   */
  void clear_pending_write_frames() noexcept;

  /**
   * @brief Request delayed close due to session takeover.
   */
  void request_session_takeover() noexcept;
  /**
   * @brief Consume pending session takeover request flag.
   * @return True when request was pending.
   */
  [[nodiscard]] bool consume_session_takeover_request() noexcept;
    void arm_session_takeover_close(
      std::chrono::steady_clock::duration delay) noexcept;
    [[nodiscard]] bool is_session_takeover_close_due(
      std::chrono::steady_clock::time_point now) const noexcept;
    /**
     * @brief Return armed takeover close deadline.
     * @return Deadline timestamp or std::nullopt.
     */
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> session_takeover_close_deadline() const noexcept;
    /**
     * @brief Clear takeover-close pending marker.
     */
    void clear_session_takeover_close_pending() noexcept;

  /**
   * @brief Return accept timestamp captured at session creation.
   * @return Accept timestamp.
   */
  [[nodiscard]] std::chrono::steady_clock::time_point accepted_at() const noexcept;

  /**
   * @brief Try to acquire decode-in-progress guard.
   * @return True when guard acquired.
   */
  [[nodiscard]] bool try_begin_decode() noexcept;
  /**
   * @brief Release decode-in-progress guard.
   */
  void end_decode() noexcept;

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
  std::atomic<bool> decode_in_progress_{false};
  bool session_takeover_close_pending_{false};
  std::chrono::steady_clock::time_point session_takeover_close_deadline_{};
  std::chrono::steady_clock::time_point accepted_at_{};
};

} // namespace mqtt
