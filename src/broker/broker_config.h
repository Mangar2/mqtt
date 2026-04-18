#pragma once

/**
 * @file broker_config.h
 * @brief BrokerConfig — all runtime configuration parameters for the broker
 *        (Module 15.1.2 + 15.1.3).
 */

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "monitoring/trace_level.h"

namespace mqtt {

/**
 * @brief One configured username/password credential entry for Module 8.2.
 */
struct PasswordCredentialConfig {
  std::string username; ///< Username matched against CONNECT username.
  std::string password; ///< Plain-text password converted to BinaryData bytes.
};

/**
 * @brief All configurable parameters for the MQTT 5.0 broker (Module 15.1).
 *
 * Instances are produced by `ConfigLoader::load()` / `ConfigLoader::parse()`.
 * Every field carries a sensible default so that minimal configuration files
 * (or even empty ones) produce a working broker.
 *
 * ### Port configuration (15.1.2)
 * - `mqtt_port`: plain MQTT over TCP.  `0` disables the listener.
 * - `ws_port`:   MQTT over WebSocket.  `0` disables the listener.
 *   MQTTS (TLS) and WSS are not supported — use a reverse proxy for TLS.
 *
 * ### Broker parameters (15.1.3)
 * The remaining fields control connection limits, protocol behaviour,
 * feature flags, and persistence.
 *
 * ### Invariants
 * `ConfigLoader` enforces these constraints when it validates the config:
 * - At least one of `mqtt_port` or `ws_port` must be non-zero.
 * - `max_connections` must be in [1, 100 000].
 * - `receive_maximum` must be in [1, 65 535].
 * - `max_queued_messages` must be in [1, 100 000].
 */
struct BrokerConfig {
  //  15.1.2  Port configuration

  /// MQTT/TCP listener port.  0 = disabled.
  uint16_t mqtt_port = 1883U;

  /// WebSocket listener port.  0 = disabled.
  uint16_t ws_port = 0U;

  //  15.1.3  Broker parameters

  /// Allow clients to connect without credentials.
  bool allow_anonymous = true;

  /// Maximum number of simultaneous client connections (1–100 000).
  uint32_t max_connections = 1000U;

  /// Per-connection maximum inflight QoS 1/2 messages (1–65 535).
  uint16_t receive_maximum = 65535U;

  /// Hard cap on Session Expiry Interval in seconds.  0 = no hard cap.
  uint32_t session_expiry_max = 0U;

  /// Maximum Topic Alias value per connection (0 = topic aliases disabled).
  uint16_t topic_alias_maximum = 10U;

  /// Per-client offline queue capacity (1–100 000).
  uint32_t max_queued_messages = 100U;

  /// Timeout in seconds before an outbound QoS 1/2 exchange is eligible for
  /// retransmission.
  uint32_t qos_retransmit_timeout_seconds = 20U;

  /// Sleep interval in milliseconds for the main loop housekeeping tick.
  uint32_t tick_interval_ms = 100U;

  /// Username/password credentials used by `PasswordAuthenticator` when
  /// anonymous access is disabled.
  std::vector<PasswordCredentialConfig> password_credentials;

  //  Persistence

  /// Enable crash-safe file persistence.
  bool persistence_enabled = false;

  /// Directory for persistence snapshot files.
  std::filesystem::path persistence_dir{"./data"};

  //  Monitoring

  /// Interval in seconds for publishing `$SYS` statistics topics.
  /// 0 = disabled (Module 16.2.2).
  uint32_t sys_topic_interval = 0U;

  //  Structured tracing (Module 26)

  /// Global structured tracing level threshold.
  TraceLevel trace_global_level = TraceLevel::Warning;

  /// Modules for which trace-level events are enabled even when global level
  /// is below `trace`.
  std::vector<std::string> trace_modules;
};

} // namespace mqtt
