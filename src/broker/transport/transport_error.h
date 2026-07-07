#pragma once

/**
 * @file transport_error.h
 * @brief TransportError enum and TransportException for the Transport Extensions module (Module 14).
 */

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mqtt {

/**
 * @brief Error codes for WebSocket and transport-layer failures.
 */
enum class TransportError : std::uint8_t {
    InvalidHandshake, ///< HTTP upgrade request is malformed or missing required headers.
    ProtocolError,    ///< WebSocket protocol violation (e.g. reserved bits set).
    FrameTooLarge,    ///< WebSocket payload exceeds the permitted maximum.
    InvalidOpcode,    ///< Unknown or disallowed WebSocket opcode.
};

/**
 * @brief Exception thrown by Transport Extension components on fatal errors.
 */
class TransportException : public std::runtime_error {
public:
    /**
     * @brief Construct with an error code and a descriptive message.
     * @param code    The transport error code that caused this exception.
     * @param message Human-readable description of the failure.
     */
    TransportException(TransportError code, std::string_view message)
        : std::runtime_error(std::string(message)), code_(code) {}

    /**
     * @brief Return the error code that caused this exception.
     */
    [[nodiscard]] TransportError code() const noexcept { return code_; }

private:
    TransportError code_; ///< Specific error that triggered this exception.
};

} // namespace mqtt
