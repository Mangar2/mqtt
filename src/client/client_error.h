#pragma once

/**
 * @file client_error.h
 * @brief ClientError enum and ClientException for client-side MQTT components.
 */

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "data_model/reason_code/reason_code.h"

namespace mqtt {

/**
 * @brief Error codes for client-side protocol and transport workflows.
 */
enum class ClientError : uint8_t {
  ResolveFailed,         ///< DNS/address resolution failed.
  SocketCreateFailed,    ///< Socket creation failed.
  ConnectFailed,         ///< TCP connect failed.
  WriteFailed,           ///< Socket write failed.
  ReadFailed,            ///< Socket read failed.
  Timeout,               ///< Operation timed out.
  ProtocolError,         ///< Peer packet violated expected protocol flow.
  NegotiationRejected,   ///< CONNECT was rejected by broker CONNACK reason.
  AliasOutOfRange,       ///< Topic alias value exceeded configured bounds.
  InvalidPacket,         ///< Packet data is invalid for requested operation.
};

/**
 * @brief Exception thrown by client-side MQTT components.
 */
class ClientException : public std::runtime_error {
public:
  /**
   * @brief Construct a client exception.
   * @param error_code Error classification.
   * @param message Human-readable description.
   * @param reason_code Optional MQTT reason code context.
   */
  explicit ClientException(
      ClientError error_code, std::string_view message,
      std::optional<ReasonCode> reason_code = std::nullopt)
      : std::runtime_error(std::string(message)), error_code_(error_code),
        reason_code_(reason_code) {}

  /**
   * @brief Return the stored error category.
   */
  [[nodiscard]] ClientError error() const noexcept { return error_code_; }

  /**
   * @brief Return the optional MQTT reason code context.
   */
  [[nodiscard]] std::optional<ReasonCode> reason_code() const noexcept {
    return reason_code_;
  }

private:
  ClientError error_code_;
  std::optional<ReasonCode> reason_code_;
};

} // namespace mqtt
