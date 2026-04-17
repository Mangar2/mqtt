#pragma once

/**
 * @file websocket_frame_codec.h
 * @brief WebSocket frame encoder and decoder for MQTT-over-WebSocket (Module 14.2.2–14.2.3).
 */

#include <cstdint>
#include <span>
#include <vector>

namespace mqtt {

/**
 * @brief WebSocket frame opcodes as defined in RFC 6455 §5.2.
 */
enum class WsOpcode : std::uint8_t {
    Continuation = 0x0U, ///< Continuation frame.
    Text         = 0x1U, ///< UTF-8 text frame.
    Binary       = 0x2U, ///< Binary data frame (used for MQTT payloads).
    Close        = 0x8U, ///< Connection close frame.
    Ping         = 0x9U, ///< Ping frame.
    Pong         = 0xAU, ///< Pong response frame.
};

/**
 * @brief A decoded WebSocket frame.
 */
struct WsFrame {
    bool fin;                    ///< `true` if this is the final fragment of a message.
    WsOpcode opcode;             ///< Frame type.
    std::vector<uint8_t> payload; ///< Unmasked payload bytes.
};

/**
 * @brief Buffers raw TCP bytes, decodes inbound WebSocket frames, and encodes
 * outbound ones (Module 14.2.2 / 14.2.3).
 *
 * ### Decode (server receiving from client)
 * ```cpp
 * WebSocketFrameCodec codec;
 * codec.append(raw_bytes);
 * while (codec.has_frame()) {
 *     WsFrame frame = codec.consume_frame();
 *     if (frame.opcode == WsOpcode::Binary) {
 *         // frame.payload contains the MQTT packet bytes (14.2.3)
 *     }
 * }
 * ```
 *
 * ### Encode (server sending to client)
 * ```cpp
 * auto bytes = WebSocketFrameCodec::encode_binary(mqtt_payload);
 * tcp_connection.write(bytes);
 * ```
 *
 * Thread safety: none — external synchronisation required.
 */
class WebSocketFrameCodec {
public:
    /// Maximum permitted payload length per frame (128 MiB).
    static constexpr std::uint64_t k_max_payload = 128ULL * 1024ULL * 1024ULL;

    /**
     * @brief Append raw bytes received from the TCP stream.
     *
     * Any number of complete frames contained in `data` are decoded and made
     * available via `consume_frame()`.  Partial frame data is retained across
     * calls until the next fragment arrives.
     *
     * @param data Incoming bytes (may be empty — no-op).
     * @throws TransportException with `FrameTooLarge` if a frame payload exceeds `k_max_payload`.
     * @throws TransportException with `ProtocolError` if RSV bits are non-zero.
     * @throws TransportException with `InvalidOpcode` if the opcode is unrecognised.
     */
    void append(std::span<const uint8_t> data);

    /**
     * @brief Return whether at least one complete decoded frame is available.
     */
    [[nodiscard]] bool has_frame() const noexcept;

    /**
     * @brief Remove and return the next decoded frame (FIFO order).
     *
     * @return The oldest buffered `WsFrame`.
     * @throws std::logic_error if `has_frame()` is `false`.
     */
    [[nodiscard]] WsFrame consume_frame();

    /**
     * @brief Encode a MQTT packet as a server-sent binary WebSocket frame (14.2.2).
     *
     * The resulting frame has FIN=1, opcode=Binary, and is never masked
     * (server-to-client frames must not be masked per RFC 6455 §5.1).
     *
     * @param payload MQTT packet bytes.
     * @return Encoded WebSocket frame bytes ready to write to the TCP socket.
     */
    [[nodiscard]] static std::vector<uint8_t> encode_binary(std::span<const uint8_t> payload);

    /**
     * @brief Encode a WebSocket control frame (Close, Ping, or Pong).
     *
     * Control frames are not fragmented and must carry at most 125 payload bytes
     * (RFC 6455 §5.5).  The frame is never masked.
     *
     * @param opcode Must be `Close`, `Ping`, or `Pong`.
     * @param payload Optional application data (≤ 125 bytes).
     * @return Encoded frame bytes.
     */
    [[nodiscard]] static std::vector<uint8_t> encode_control(
        WsOpcode opcode,
        std::span<const uint8_t> payload = {});

private:
    std::vector<uint8_t> buf_;      ///< Accumulated undecoded bytes.
    std::vector<WsFrame> frames_;   ///< Fully decoded frames waiting to be consumed.

    /** @brief Attempt to decode as many complete frames from `buf_` as possible. */
    void try_decode_();

    /** @brief Encode a single frame header + payload without masking. */
    [[nodiscard]] static std::vector<uint8_t> encode_frame_(
        bool fin,
        WsOpcode opcode,
        std::span<const uint8_t> payload);
};

} // namespace mqtt
