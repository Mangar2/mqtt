#pragma once

/**
 * @file codec_error.h
 * @brief MQTT codec error codes and exception type.
 */

#include <cstdint>
#include <stdexcept>
#include <string>

namespace mqtt {

/**
 * @brief Error codes produced by the MQTT codec module.
 */
enum class CodecError : uint8_t {
    BufferTooShort,               ///< Input ran out of bytes before decode completed.
    StringTooLong,                ///< UTF-8 string or binary data exceeds 65 535 bytes.
    VariableByteIntegerOverflow,  ///< VBI value exceeds max, or continuation bit set in byte 4.
    InvalidPropertyId,            ///< Property ID does not correspond to any known PropertyId.
    PropertyTypeMismatch,         ///< Property value type does not match the expected type for its ID.
    DuplicateProperty,            ///< A non-repeatable property appears more than once.
    PropertyNotAllowed,           ///< Property is not permitted in the given packet type.
    InvalidPacketType,            ///< Fixed-header type nibble is 0 (reserved).
    InvalidFlags,                 ///< Fixed-header flags are not valid for the packet type.
};

/**
 * @brief Exception thrown when a codec encode or decode operation fails.
 *
 * Carries a machine-readable @p CodecError alongside the human-readable
 * `what()` string inherited from `std::runtime_error`.
 */
class CodecException : public std::runtime_error {
public:
    /**
     * @brief Constructs a CodecException.
     * @param error  Machine-readable error code.
     * @param msg    Human-readable description; forwarded to `std::runtime_error`.
     */
    CodecException(CodecError error, const std::string& msg)
        : std::runtime_error(msg)
        , error_(error)
    {}

    /**
     * @brief Returns the error code associated with this exception.
     * @return The CodecError that describes the failure.
     */
    [[nodiscard]] CodecError error() const noexcept { return error_; }

private:
    CodecError error_;  ///< Machine-readable error code.
};

} // namespace mqtt
