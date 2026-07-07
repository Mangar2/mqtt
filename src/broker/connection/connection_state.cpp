#include "connection/connection_state.h"

#include "connection/connection_error.h"

namespace mqtt {

void ConnectionStateMachine::on_connect() {
  if (state_ == ConnectionState::Connected) {
    throw ConnectionException(
        ConnectionError::DuplicateConnect,
        "CONNECT received on an already-established connection");
  }
  if (state_ == ConnectionState::Disconnecting ||
      state_ == ConnectionState::Closed) {
    throw ConnectionException(ConnectionError::InvalidState,
                              "CONNECT received after connection teardown");
  }
  state_ = ConnectionState::Connected;
}

void ConnectionStateMachine::on_disconnect() {
  if (state_ != ConnectionState::Connected) {
    throw ConnectionException(ConnectionError::InvalidState,
                              "DISCONNECT received outside of Connected state");
  }
  state_ = ConnectionState::Disconnecting;
}

void ConnectionStateMachine::on_connection_lost() noexcept {
  state_ = ConnectionState::Closed;
}

void ConnectionStateMachine::close() noexcept {
  state_ = ConnectionState::Closed;
}

void ConnectionStateMachine::enforce_not_connecting() const {
  if (state_ == ConnectionState::Connecting) {
    throw ConnectionException(ConnectionError::ConnectRequired,
                              "Packet received before CONNECT");
  }
  if (state_ == ConnectionState::Disconnecting ||
      state_ == ConnectionState::Closed) {
    throw ConnectionException(ConnectionError::InvalidState,
                              "Packet received after connection teardown");
  }
}

ConnectionState ConnectionStateMachine::state() const noexcept {
  return state_;
}

bool ConnectionStateMachine::is_connected() const noexcept {
  return state_ == ConnectionState::Connected;
}

} // namespace mqtt
