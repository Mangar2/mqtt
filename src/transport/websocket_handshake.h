#pragma once

/**
 * @file websocket_handshake.h
 * @brief WebSocket HTTP/1.1 upgrade handshake for MQTT-over-WebSocket (Module 14.2.1).
 */

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mqtt {

/**
 * @brief Parses an HTTP/1.1 WebSocket upgrade request and produces a 101 response.
 *
 * Call `append()` with raw bytes received from the TCP stream.  Once
 * `is_complete()` returns `true`, call `build_response()` to obtain the HTTP
 * 101 bytes to send back to the client, then hand off subsequent bytes to
 * `WebSocketFrameCodec`.
 *
 * Validates the following requirements from RFC 6455 §4.2.1:
 * - `Upgrade: websocket` header (case-insensitive)
 * - `Connection: Upgrade` header (case-insensitive)
 * - Presence of the `Sec-WebSocket-Key` header
 * - `Sec-WebSocket-Version: 13`
 *
 * The `Sec-WebSocket-Accept` key in the response is computed as
 * `base64(sha1(key + GUID))` per RFC 6455 §4.2.2.
 *
 * Thread safety: none — external synchronisation required.
 */
class WebSocketHandshake {
public:
    /**
     * @brief Append raw bytes from the TCP stream.
     *
     * Parsing completes when the full HTTP header block (terminated by
     * `\r\n\r\n`) has been received and all mandatory WebSocket headers are
     * present and valid.
     *
     * @param data Incoming bytes (may be empty — no-op).
     * @throws TransportException with `InvalidHandshake` on any RFC 6455 violation.
     */
    void append(std::span<const uint8_t> data);

    /**
     * @brief Return whether a valid HTTP upgrade request has been fully parsed.
     * @return `true` once `append()` has successfully processed a complete request.
     */
    [[nodiscard]] bool is_complete() const noexcept;

    /**
     * @brief Build the HTTP 101 Switching Protocols response.
     *
     * @pre `is_complete()` must return `true`.
     * @return Response bytes ready to write to the TCP socket.
     * @throws std::logic_error if `is_complete()` is `false`.
     */
    [[nodiscard]] std::vector<uint8_t> build_response() const;

private:
    std::string raw_;        ///< Accumulated raw request bytes.
    std::string ws_key_;     ///< Extracted value of the Sec-WebSocket-Key header.
    bool complete_ = false;  ///< Set to true after successful parse.

    /** @brief Parse accumulated `raw_` for a complete and valid upgrade request. */
    void parse_();
};

} // namespace mqtt
