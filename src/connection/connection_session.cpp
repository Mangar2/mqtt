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
      topic_alias_table_(config.topic_alias_maximum),
      inbound_receive_window_(config.receive_maximum) {}

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

} // namespace mqtt
