#pragma once

/**
 * @file rs485_serial_protocol.h
 * @brief RS485 serial codec and stream-reader API.
 */


#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yaha {

inline constexpr std::size_t k_rs485_message_size_v0{7U};
inline constexpr std::size_t k_rs485_message_size_v1{9U};

/**
 * @brief One decoded/encodable RS485 frame.
 */
struct Rs485SerialMessage {
    std::uint8_t sender{0U};
    std::uint8_t receiver{0U};
    bool reply{false};
    char command{'\0'};
    double value{0.0};
    std::uint8_t parity{0U};
    std::uint16_t crc16{0U};
    std::uint8_t version{1U};
    std::size_t length{k_rs485_message_size_v1};

    /**
     * @brief Returns true for token/internal command frames.
     * @return True if command is '!'.
     */
    [[nodiscard]] bool isInternal() const noexcept;
};

/**
 * @brief One stream-reader decode result.
 */
struct Rs485ReadResult {
    std::size_t startIndex{0U};
    std::optional<Rs485SerialMessage> message{};
    std::string hex{};
    std::string error{};
};

/**
 * @brief Calculates CCITT CRC16 over a byte range.
 * @param byteArray Source bytes.
 * @param startIndex First index to include.
 * @param length Number of bytes to include.
 * @return CRC16 value.
 */
[[nodiscard]] std::uint16_t calcRs485Crc16(
    const std::vector<std::uint8_t>& byteArray,
    std::size_t startIndex,
    std::size_t length);

/**
 * @brief Calculates XOR parity over a byte range.
 * @param byteArray Source bytes.
 * @param startIndex First index to include.
 * @param length Number of bytes to include.
 * @return Parity byte.
 */
[[nodiscard]] std::uint8_t calcRs485Parity(
    const std::vector<std::uint8_t>& byteArray,
    std::size_t startIndex,
    std::size_t length);

/**
 * @brief Decodes one RS485 frame at a byte offset.
 * @param byteArray Source bytes.
 * @param startIndex Start index of potential frame.
 * @return Parsed message output.
 * @throws YahaError on parse errors.
 */
[[nodiscard]] Rs485SerialMessage decodeRs485SerialMessage(
    const std::vector<std::uint8_t>& byteArray,
    std::size_t startIndex);

/**
 * @brief Encodes one RS485 frame.
 * @param message Message to encode.
 * @return Encoded byte vector.
 * @throws YahaError for unsupported version.
 */
[[nodiscard]] std::vector<std::uint8_t> encodeRs485SerialMessage(const Rs485SerialMessage& message);

/**
 * @brief Reads all decodable frames from one byte chunk.
 */
class Rs485StreamReader {
public:
    /**
     * @brief Parses a stream chunk into success/error frame results.
     * @param byteArray Stream chunk bytes.
     * @return Ordered decode results.
     */
    [[nodiscard]] static std::vector<Rs485ReadResult> read(const std::vector<std::uint8_t>& byteArray);

private:
    [[nodiscard]] static std::size_t skipNoise(
        const std::vector<std::uint8_t>& byteArray,
        std::size_t startIndex);

    [[nodiscard]] static std::optional<Rs485ReadResult> readMessage(
        const std::vector<std::uint8_t>& byteArray,
        std::size_t startIndex);
};

} // namespace yaha
