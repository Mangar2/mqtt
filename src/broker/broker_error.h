#pragma once

/**
 * @file broker_error.h
 * @brief BrokerError enum and BrokerException for the Broker Orchestrator
 *        (Module 15).
 */

#include <cstdint>
#include <stdexcept>
#include <string>

namespace mqtt {

/**
 * @brief Error codes for the Broker Orchestrator (Module 15).
 */
enum class BrokerError : uint8_t {
  InvalidConfig,        ///< A configuration value is out of range or malformed.
  NoListenerConfigured, ///< Neither mqtt_port nor ws_port is non-zero.
  AlreadyRunning, ///< startup() called while the broker is already running.
  NotRunning,     ///< shutdown() called when the broker is not running.
};

/**
 * @brief Exception thrown by the Broker Orchestrator on unrecoverable errors.
 */
class BrokerException : public std::runtime_error {
public:
  /**
   * @brief Construct a BrokerException.
   * @param error Error code describing the failure.
   * @param msg   Human-readable description.
   */
  BrokerException(BrokerError error, const std::string &msg)
      : std::runtime_error(msg), error_(error) {}

  /**
   * @brief Return the error code.
   * @return `BrokerError` value identifying the failure category.
   */
  [[nodiscard]] BrokerError error() const noexcept { return error_; }

private:
  BrokerError error_; ///< Categorised error code.
};

} // namespace mqtt
