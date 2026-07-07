#pragma once

/**
 * @file connection_error.h
 * @brief ConnectionError enum and ConnectionException for the Connection
 * Handler (Module 7).
 */

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "codec/codec_error.h"
#include "data_model/reason_code/reason_code.h"

namespace mqtt {

/**
 * @brief Error codes for connection lifecycle violations in Module 7.
 */
enum class ConnectionError : uint8_t {
  DuplicateConnect, ///< A second CONNECT packet was received on an established
                    ///< connection.
  ConnectRequired,  ///< A non-CONNECT packet was received before the initial
                    ///< CONNECT.
  InvalidState,  ///< An operation was attempted in an incompatible connection
                 ///< state.
  AliasNotFound, ///< A topic alias lookup found no mapping for the given alias
                 ///< value.
  AliasOutOfRange, ///< A topic alias value exceeds the negotiated Topic Alias
                   ///< Maximum or is zero.
};

/**
 * @brief Exception thrown by Connection Handler components on protocol or state
 * violations.
 *
 * Derives from `std::runtime_error`. Callers use `error()` to branch on the
 * specific failure code without parsing the human-readable message.
 */
class ConnectionException : public std::runtime_error {
public:
  /**
   * @brief Construct a ConnectionException.
   * @param err Error code identifying the violation.
   * @param msg Human-readable description.
   */
  explicit ConnectionException(ConnectionError err, std::string_view msg)
      : std::runtime_error(std::string(msg)), error_(err) {}

  /**
   * @brief Return the error code.
   * @return The `ConnectionError` that caused this exception.
   */
  [[nodiscard]] ConnectionError error() const noexcept { return error_; }

private:
  ConnectionError error_; ///< Error code stored for programmatic inspection.
};

/**
 * @brief Map a codec decode error during CONNECT handling to MQTT reason code.
 * @param codec_error Codec error emitted by packet decode.
 * @return MQTT reason code suitable for CONNACK failure.
 */
[[nodiscard]] constexpr ReasonCode
map_codec_error_to_connect_reason(CodecError codec_error) noexcept {
  switch (codec_error) {
  case CodecError::InvalidProtocolVersion:
    return ReasonCode::UnsupportedProtocolVersion;
  case CodecError::BufferTooShort:
  case CodecError::StringTooLong:
  case CodecError::VariableByteIntegerOverflow:
  case CodecError::InvalidPropertyId:
  case CodecError::PropertyTypeMismatch:
  case CodecError::DuplicateProperty:
  case CodecError::PropertyNotAllowed:
  case CodecError::InvalidPacketType:
  case CodecError::InvalidFlags:
  case CodecError::InvalidProtocolName:
  case CodecError::InvalidQoS:
  case CodecError::MalformedPacket:
    return ReasonCode::MalformedPacket;
  }

  return ReasonCode::ProtocolError;
}

/**
 * @brief Map a codec decode error after CONNECT handling to MQTT reason code.
 * @param codec_error Codec error emitted by packet decode.
 * @return MQTT reason code suitable for DISCONNECT from runtime loop.
 */
[[nodiscard]] constexpr ReasonCode
map_codec_error_to_runtime_reason(CodecError codec_error) noexcept {
  switch (codec_error) {
  case CodecError::DuplicateProperty:
  case CodecError::InvalidPacketType:
    return ReasonCode::ProtocolError;

  case CodecError::BufferTooShort:
  case CodecError::StringTooLong:
  case CodecError::VariableByteIntegerOverflow:
  case CodecError::InvalidPropertyId:
  case CodecError::PropertyTypeMismatch:
  case CodecError::PropertyNotAllowed:
  case CodecError::InvalidFlags:
  case CodecError::InvalidProtocolName:
  case CodecError::InvalidProtocolVersion:
  case CodecError::InvalidQoS:
  case CodecError::MalformedPacket:
    return ReasonCode::MalformedPacket;
  }

  return ReasonCode::ProtocolError;
}

} // namespace mqtt
