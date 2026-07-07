#pragma once

/**
 * @file connection_state.h
 * @brief ConnectionStateMachine — lifecycle state management for a single MQTT
 * client connection (Module 7.1).
 */

#include <cstdint>

namespace mqtt {

/**
 * @brief States of a single MQTT 5.0 client connection.
 */
enum class ConnectionState : uint8_t {
  Connecting,    ///< TCP accepted; CONNECT not yet received.
  Connected,     ///< CONNECT received and accepted; session active.
  Disconnecting, ///< DISCONNECT received or broker-initiated close in progress.
  Closed,        ///< Connection fully terminated.
};

/**
 * @brief Manages the lifecycle state of a single MQTT 5.0 client connection
 * (Module 7.1).
 *
 * Tracks the four-phase connection lifecycle and enforces protocol rules:
 * - CONNECT must be the first packet (7.1.2).
 * - Duplicate CONNECT on an established connection is rejected (7.1.3).
 * - Abrupt connection loss transitions directly to Closed (7.1.4).
 *
 * All invalid transitions throw `ConnectionException`.
 *
 * Thread safety: none — external synchronisation required.
 */
class ConnectionStateMachine {
public:
  /** @brief Construct in initial `Connecting` state. */
  ConnectionStateMachine() noexcept = default;

  /**
   * @brief Process a received CONNECT packet (7.1.2, 7.1.3).
   *
   * Transitions `Connecting` → `Connected`.
   *
   * @throws ConnectionException(DuplicateConnect) if state is `Connected`.
   * @throws ConnectionException(InvalidState) if state is `Disconnecting` or
   * `Closed`.
   */
  void on_connect();

  /**
   * @brief Process a received DISCONNECT packet.
   *
   * Transitions `Connected` → `Disconnecting`.
   *
   * @throws ConnectionException(InvalidState) if state is not `Connected`.
   */
  void on_disconnect();

  /**
   * @brief Handle abrupt TCP connection loss (7.1.4).
   *
   * Transitions any state → `Closed`.
   */
  void on_connection_lost() noexcept;

  /**
   * @brief Force-close the connection.
   *
   * Transitions any state → `Closed`.
   */
  void close() noexcept;

  /**
   * @brief Enforce that CONNECT has already been received (7.1.2).
   *
   * Call this before processing any non-CONNECT packet.
   *
   * @throws ConnectionException(ConnectRequired) if state is `Connecting`.
   * @throws ConnectionException(InvalidState) if state is `Disconnecting` or
   * `Closed`.
   */
  void enforce_not_connecting() const;

  /**
   * @brief Return the current connection state.
   * @return Current `ConnectionState`.
   */
  [[nodiscard]] ConnectionState state() const noexcept;

  /**
   * @brief Return true if the connection is fully established.
   * @return `true` when state is `Connected`.
   */
  [[nodiscard]] bool is_connected() const noexcept;

private:
  ConnectionState state_{
      ConnectionState::Connecting}; ///< Current lifecycle state.
};

} // namespace mqtt
