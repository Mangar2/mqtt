#pragma once

/**
 * @file store_error.h
 * @brief Error types for the In-Memory Store module (Module 4).
 */

#include <cstdint>
#include <stdexcept>
#include <string>

namespace mqtt {

/**
 * @brief Error codes for the In-Memory Store module.
 */
enum class StoreError : uint8_t {
  SessionAlreadyExists, ///< create() called for a client_id already present.
  PacketIdNotFound, ///< update() called for a packet_id/direction not in the
                    ///< store.
};

/**
 * @brief Exception thrown by the In-Memory Store module on unrecoverable state
 * violations.
 *
 * Derives from std::runtime_error as required by project conventions.
 */
class StoreException : public std::runtime_error {
public:
  /**
   * @brief Construct a StoreException.
   * @param err Error code identifying the violation.
   * @param msg Human-readable description of the error.
   */
  explicit StoreException(StoreError err, const std::string &msg)
      : std::runtime_error(msg), error_(err) {}

  /**
   * @brief Return the error code.
   * @return The StoreError that caused this exception.
   */
  [[nodiscard]] StoreError error() const noexcept { return error_; }

private:
  StoreError error_; ///< Error code stored for programmatic inspection.
};

} // namespace mqtt
