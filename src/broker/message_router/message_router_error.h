#pragma once

/**
 * @file message_router_error.h
 * @brief Error types for the Message Router module (Module 12).
 */

#include <cstdint>
#include <stdexcept>
#include <string>

namespace mqtt {

/**
 * @brief Error codes produced by the Message Router module.
 */
enum class MessageRouterError : uint8_t {
  PublishNotAuthorized, ///< Client lacks publish permission for the resolved
                        ///< topic.
  TopicAliasInvalid, ///< Topic Alias value is 0, out of range, or references an
                     ///< unregistered alias.
  QueueFull, ///< Offline queue size limit reached; the message is discarded.
};

/**
 * @brief Exception thrown by the Message Router module on protocol or policy
 * violations.
 */
class MessageRouterException : public std::runtime_error {
public:
  /**
   * @brief Construct a MessageRouterException.
   * @param err Error code identifying the violation.
   * @param msg Human-readable description of the error.
   */
  explicit MessageRouterException(MessageRouterError err,
                                  const std::string &msg)
      : std::runtime_error(msg), error_(err) {}

  /**
   * @brief Return the error code.
   * @return The MessageRouterError that caused this exception.
   */
  [[nodiscard]] MessageRouterError error() const noexcept { return error_; }

private:
  MessageRouterError error_; ///< Error code for programmatic inspection.
};

} // namespace mqtt
