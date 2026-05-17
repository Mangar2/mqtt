#include "yaha/rs485_protocol/rs485_serial_protocol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <stdexcept>
#include <string>

namespace yaha {
namespace {

constexpr std::uint16_t k_crc_start_value{0xFFFFU};
constexpr std::uint16_t k_crc_polynom{0x1021U};
constexpr std::uint16_t k_crc_msb_mask{0x8000U};
constexpr std::uint16_t k_crc_mask{0xFFFFU};
constexpr std::uint8_t k_nibble_mask{0x0FU};
constexpr std::uint8_t k_max_address{127U};
constexpr std::size_t k_bits_in_byte{8U};
constexpr std::uint8_t k_u8_low_mask{0xFFU};
constexpr double k_u16_max_as_double{65535.0};

constexpr std::size_t k_field_sender{0U};
constexpr std::size_t k_field_receiver{1U};
constexpr std::size_t k_field_flags{2U};
constexpr std::size_t k_field_v1_length{3U};
constexpr std::size_t k_field_v0_command{3U};
constexpr std::size_t k_field_v1_command{4U};
constexpr std::size_t k_field_v0_value_high{4U};
constexpr std::size_t k_field_v0_value_low{5U};
constexpr std::size_t k_field_v1_value_high{5U};
constexpr std::size_t k_field_v1_value_low{6U};
constexpr std::size_t k_field_v0_parity{6U};
constexpr std::size_t k_field_v1_crc_low{7U};
constexpr std::size_t k_field_v1_crc_high{8U};

[[nodiscard]] std::string toHexString(
    const std::vector<std::uint8_t>& byteArray,
    const std::size_t startIndex,
    const std::size_t length) {
    static constexpr std::array<char, 16U> hexDigits{
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

    if (startIndex >= byteArray.size() || length == 0U) {
        return {};
    }

    const std::size_t cappedLength = std::min(length, byteArray.size() - startIndex);
    std::string output{};
    output.reserve(cappedLength * 3U);

    for (std::size_t index = 0U; index < cappedLength; ++index) {
        const std::uint8_t value = byteArray[startIndex + index];
        output.push_back(hexDigits[value >> 4U]);
        output.push_back(hexDigits[value & k_nibble_mask]);
        if (index + 1U < cappedLength) {
            output.push_back(' ');
        }
    }

    return output;
}

[[nodiscard]] std::uint16_t valueToRawWord(const double value) {
    if (!std::isfinite(value)) {
        throw std::runtime_error("invalid value for encode: non-finite");
    }

    if (value < 0.0 || value > k_u16_max_as_double) {
        throw std::runtime_error("invalid value for encode: out of range 0..65535");
    }

    return static_cast<std::uint16_t>(value);
}

[[nodiscard]] double decodeValueByCommand(
    const char command,
    const std::uint8_t valueHigh,
    const std::uint8_t valueLow) {
    if (command == 'h' || command == 't' || command == 's') {
        return static_cast<double>(valueHigh) + (static_cast<double>(valueLow) / 100.0);
    }

    return static_cast<double>((static_cast<std::uint16_t>(valueHigh) << k_bits_in_byte) | valueLow);
}

[[nodiscard]] std::uint8_t buildFlags(const bool reply, const std::uint8_t version) {
    return static_cast<std::uint8_t>((reply ? 1U : 0U) + static_cast<std::uint8_t>(version << 1U));
}

void ensureHeaderReadable(const std::vector<std::uint8_t>& byteArray, const std::size_t startIndex) {
    if (startIndex >= byteArray.size()) {
        throw std::runtime_error("Insufficient data received");
    }
    if (byteArray.size() < startIndex + 3U) {
        throw std::runtime_error("Insufficient data received");
    }
}

} // namespace

bool Rs485SerialMessage::isInternal() const noexcept {
    return command == '!';
}

std::uint16_t calcRs485Crc16(
    const std::vector<std::uint8_t>& byteArray,
    const std::size_t startIndex,
    const std::size_t length) {
    std::uint16_t crc = k_crc_start_value;
    const std::size_t endIndex = std::min(startIndex + length, byteArray.size());

    for (std::size_t index = startIndex; index < endIndex; ++index) {
        crc ^= static_cast<std::uint16_t>(byteArray[index] << k_bits_in_byte);
        for (std::size_t shift = 0U; shift < k_bits_in_byte; ++shift) {
            if ((crc & k_crc_msb_mask) != 0U) {
                crc = static_cast<std::uint16_t>((crc << 1U) ^ k_crc_polynom);
            } else {
                crc = static_cast<std::uint16_t>(crc << 1U);
            }
        }
    }

    return static_cast<std::uint16_t>(crc & k_crc_mask);
}

std::uint8_t calcRs485Parity(
    const std::vector<std::uint8_t>& byteArray,
    const std::size_t startIndex,
    const std::size_t length) {
    std::uint8_t parity{0U};
    if (startIndex >= byteArray.size()) {
        return parity;
    }

    const std::size_t endIndex = std::min(startIndex + length, byteArray.size());
    for (std::size_t index = startIndex; index < endIndex; ++index) {
        parity ^= byteArray[index];
    }

    return parity;
}

void decodeRs485SerialMessage(
    const std::vector<std::uint8_t>& byteArray,
    const std::size_t startIndex,
    Rs485SerialMessage& output) {
    ensureHeaderReadable(byteArray, startIndex);

    output = Rs485SerialMessage{};
    output.sender = byteArray[startIndex + k_field_sender];
    output.receiver = byteArray[startIndex + k_field_receiver];
    const std::uint8_t flags = byteArray[startIndex + k_field_flags];
    output.reply = (flags & 1U) == 1U;
    output.version = static_cast<std::uint8_t>(flags >> 1U);

    switch (output.version) {
    case 0U:
        output.length = k_rs485_message_size_v0;
        break;
    case 1U:
        if (byteArray.size() < startIndex + k_field_v1_command) {
            throw std::runtime_error("Insufficient data received");
        }
        output.length = byteArray[startIndex + k_field_v1_length];
        break;
    default:
        output.length = k_rs485_message_size_v0;
        break;
    }

    if (output.sender > k_max_address) {
        throw std::runtime_error(std::format("Illegal sender address {}", output.sender));
    }

    if (output.receiver > k_max_address) {
        throw std::runtime_error(std::format("Illegal receiver address {}", output.receiver));
    }

    if (output.version == 0U) {
        if (byteArray.size() < startIndex + output.length) {
            throw std::runtime_error("Insufficient data received");
        }

        output.command = static_cast<char>(byteArray[startIndex + k_field_v0_command]);
        const std::uint8_t valueHigh = byteArray[startIndex + k_field_v0_value_high];
        const std::uint8_t valueLow = byteArray[startIndex + k_field_v0_value_low];
        output.value = decodeValueByCommand(output.command, valueHigh, valueLow);
        output.parity = byteArray[startIndex + k_field_v0_parity];

        const std::uint8_t calculatedParity = calcRs485Parity(byteArray, startIndex, k_rs485_message_size_v0 - 1U);
        if (calculatedParity != output.parity) {
            throw std::runtime_error(std::format(
                "Parity does not match, expected: {} received: {}",
                calculatedParity,
                output.parity));
        }
        return;
    }

    if (output.version == 1U) {
        if (byteArray.size() < startIndex + output.length) {
            throw std::runtime_error("Insufficient data received");
        }

        if (output.length != k_rs485_message_size_v1) {
            throw std::runtime_error(std::format(
                "Illegal message length, expected: {} received: {}",
            k_rs485_message_size_v1,
                output.length));
        }

        output.command = static_cast<char>(byteArray[startIndex + k_field_v1_command]);
        const std::uint8_t valueHigh = byteArray[startIndex + k_field_v1_value_high];
        const std::uint8_t valueLow = byteArray[startIndex + k_field_v1_value_low];
        output.value = decodeValueByCommand(output.command, valueHigh, valueLow);

        const std::uint16_t calculatedCrc16 = calcRs485Crc16(byteArray, startIndex, k_rs485_message_size_v1 - 2U);
        const std::uint8_t crcLow = byteArray[startIndex + k_field_v1_crc_low];
        const std::uint8_t crcHigh = byteArray[startIndex + k_field_v1_crc_high];
        output.crc16 = static_cast<std::uint16_t>((static_cast<std::uint16_t>(crcHigh) << k_bits_in_byte) | crcLow);

        if (output.crc16 != calculatedCrc16) {
            throw std::runtime_error(std::format(
                "CRC does not match. Expected: {:x} Found: {:x}",
                calculatedCrc16,
                output.crc16));
        }
        return;
    }

    throw std::runtime_error(std::format("Version not supported: {}", output.version));
}

std::vector<std::uint8_t> encodeRs485SerialMessage(const Rs485SerialMessage& message) {
    const std::uint16_t rawValue = valueToRawWord(message.value);
    const auto valueHigh = static_cast<std::uint8_t>(rawValue >> k_bits_in_byte);
    const auto valueLow = static_cast<std::uint8_t>(rawValue & k_u8_low_mask);

    if (message.version == 0U) {
        std::vector<std::uint8_t> byteArray(k_rs485_message_size_v0, 0U);
        byteArray[k_field_sender] = message.sender;
        byteArray[k_field_receiver] = message.receiver;
        byteArray[k_field_flags] = buildFlags(message.reply, message.version);
        byteArray[k_field_v0_command] = static_cast<std::uint8_t>(message.command);
        byteArray[k_field_v0_value_high] = valueHigh;
        byteArray[k_field_v0_value_low] = valueLow;
        byteArray[k_field_v0_parity] = calcRs485Parity(byteArray, 0U, k_rs485_message_size_v0 - 1U);
        return byteArray;
    }

    if (message.version == 1U) {
        std::vector<std::uint8_t> byteArray(k_rs485_message_size_v1, 0U);
        byteArray[k_field_sender] = message.sender;
        byteArray[k_field_receiver] = message.receiver;
        byteArray[k_field_flags] = buildFlags(message.reply, message.version);
        byteArray[k_field_v1_length] = static_cast<std::uint8_t>(k_rs485_message_size_v1);
        byteArray[k_field_v1_command] = static_cast<std::uint8_t>(message.command);
        byteArray[k_field_v1_value_high] = valueHigh;
        byteArray[k_field_v1_value_low] = valueLow;

        const std::uint16_t crc = calcRs485Crc16(byteArray, 0U, 7U);
        byteArray[k_field_v1_crc_low] = static_cast<std::uint8_t>(crc & k_u8_low_mask);
        byteArray[k_field_v1_crc_high] = static_cast<std::uint8_t>(crc >> k_bits_in_byte);
        return byteArray;
    }

    throw std::runtime_error(std::format("Unsupported message version {}", message.version));
}

std::size_t Rs485StreamReader::skipNoise(
    const std::vector<std::uint8_t>& byteArray,
    std::size_t startIndex) {
    while (startIndex < byteArray.size() &&
           (byteArray[startIndex] == 0U || byteArray[startIndex] > k_max_address)) {
        ++startIndex;
    }
    return startIndex;
}

std::optional<Rs485ReadResult> Rs485StreamReader::readMessage(
    const std::vector<std::uint8_t>& byteArray,
    const std::size_t initialStartIndex) {
    const std::size_t startIndex = skipNoise(byteArray, initialStartIndex);
    if (startIndex >= byteArray.size()) {
        return std::nullopt;
    }

    Rs485SerialMessage message{};
    try {
        decodeRs485SerialMessage(byteArray, startIndex, message);
        const std::string hex = toHexString(byteArray, startIndex, message.length);
        return Rs485ReadResult{
            .startIndex = startIndex + message.length,
            .message = message,
            .hex = hex,
            .error = {}};
    } catch (const std::exception& exception) {
        const std::string hex = toHexString(byteArray, startIndex, byteArray.size() - startIndex);
        return Rs485ReadResult{
            .startIndex = startIndex + message.length,
            .message = std::nullopt,
            .hex = hex,
            .error = exception.what()};
    }
}

std::vector<Rs485ReadResult> Rs485StreamReader::read(
    const std::vector<std::uint8_t>& byteArray) {
    std::vector<Rs485ReadResult> results{};
    std::size_t startIndex{0U};

    while (true) {
        const auto result = readMessage(byteArray, startIndex);
        if (!result.has_value()) {
            break;
        }
        results.push_back(*result);
        startIndex = result->startIndex;
    }

    return results;
}

} // namespace yaha
