#include "connection/connection_session.h"

#include <utility>

#include "client_session/client_session.h"

namespace mqtt {

ConnectionSession::ConnectionSession(
    std::unique_ptr<TcpConnection> connection,
    std::unique_ptr<WebSocketTransport> ws_transport, bool is_websocket,
    const BrokerConfig &config)
    : connection_(std::move(connection)),
      ws_transport_(std::move(ws_transport)),
      is_websocket_(is_websocket),
    stream_buffer_(StreamBufferConfig{.chunk_size =
                      StreamBufferConfig::k_default_chunk_size,
                    .max_buffered =
                      static_cast<std::size_t>(config.stream_buffer_max_bytes),
                    .free_list_max =
                      StreamBufferConfig::k_default_free_list_max}),
      topic_alias_table_(config.topic_alias_maximum),
      inbound_receive_window_(config.receive_maximum),
      accepted_at_(std::chrono::steady_clock::now()) {}

    ConnectionSession::~ConnectionSession() = default;

TcpConnection &ConnectionSession::connection() noexcept { return *connection_; }

WebSocketTransport *ConnectionSession::ws_transport() noexcept {
  return ws_transport_.get();
}

bool ConnectionSession::is_websocket() const noexcept { return is_websocket_; }

StreamBuffer &ConnectionSession::stream_buffer() noexcept { return stream_buffer_; }

TopicAliasTable &ConnectionSession::topic_alias_table() noexcept {
  return topic_alias_table_;
}

ReceiveMaximum &ConnectionSession::inbound_receive_window() noexcept {
  return inbound_receive_window_;
}

void ConnectionSession::install_client_session(
    std::unique_ptr<ClientSession> client_session) {
  client_session_ = std::move(client_session);
}

ClientSession *ConnectionSession::client_session() noexcept {
  return client_session_.get();
}

const ClientSession *ConnectionSession::client_session() const noexcept {
  return client_session_.get();
}

std::shared_ptr<OutboundQueue> ConnectionSession::outbound_queue() noexcept {
  if (client_session_ == nullptr) {
    return nullptr;
  }
  return client_session_->outbound_queue();
}

ConnectionSession::Phase ConnectionSession::phase() const noexcept {
  return phase_;
}

void ConnectionSession::set_phase(Phase phase) noexcept { phase_ = phase; }

RuntimeDisconnectState &ConnectionSession::disconnect_state() noexcept {
  return disconnect_state_;
}

std::optional<ConnectPacket> &ConnectionSession::connect_packet() noexcept {
  return connect_packet_;
}

ConnectResult &ConnectionSession::connect_result() noexcept {
  return connect_result_;
}

std::vector<WriteBuffer> &ConnectionSession::pending_write_frames() noexcept {
  return pending_write_frames_;
}

void ConnectionSession::clear_pending_write_frames() noexcept {
  pending_write_frames_.clear();
}

void ConnectionSession::request_session_takeover() noexcept {
  session_takeover_requested_.store(true, std::memory_order_release);
}

bool ConnectionSession::consume_session_takeover_request() noexcept {
  return session_takeover_requested_.exchange(false, std::memory_order_acq_rel);
}

void ConnectionSession::arm_session_takeover_close(
    std::chrono::steady_clock::duration delay) noexcept {
  session_takeover_close_pending_ = true;
  session_takeover_close_deadline_ = std::chrono::steady_clock::now() + delay;
}

bool ConnectionSession::is_session_takeover_close_due(
    std::chrono::steady_clock::time_point now) const noexcept {
  return session_takeover_close_pending_ && now >= session_takeover_close_deadline_;
}

std::optional<std::chrono::steady_clock::time_point>
ConnectionSession::session_takeover_close_deadline() const noexcept {
  if (!session_takeover_close_pending_) {
    return std::nullopt;
  }
  return session_takeover_close_deadline_;
}

void ConnectionSession::clear_session_takeover_close_pending() noexcept {
  session_takeover_close_pending_ = false;
}

std::chrono::steady_clock::time_point ConnectionSession::accepted_at() const
    noexcept {
  return accepted_at_;
}

bool ConnectionSession::try_begin_decode() noexcept {
  bool expected = false;
  return decode_in_progress_.compare_exchange_strong(expected, true,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_acquire);
}

void ConnectionSession::end_decode() noexcept {
  decode_in_progress_.store(false, std::memory_order_release);
}

} // namespace mqtt
