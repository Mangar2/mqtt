#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "yaha/rs485_protocol/rs485_serial_protocol.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr double k_comparison_epsilon{1e-9};
constexpr std::uint8_t k_corrupt_crc_mask{0xFFU};
constexpr std::uint8_t k_invalid_noise_byte{255U};
constexpr std::size_t k_crc_high_index{8U};
constexpr std::size_t k_crc_low_index{7U};

constexpr std::uint8_t k_addr_2{2U};
constexpr std::uint8_t k_addr_3{3U};
constexpr std::uint8_t k_addr_4{4U};
constexpr std::uint8_t k_addr_5{5U};
constexpr std::uint8_t k_addr_6{6U};
constexpr std::uint8_t k_addr_7{7U};
constexpr std::uint8_t k_addr_8{8U};
constexpr std::uint8_t k_addr_9{9U};

constexpr double k_value_11{11.0};
constexpr double k_value_33{33.0};
constexpr double k_value_66{66.0};
constexpr double k_value_88{88.0};
constexpr double k_value_900{900.0};
constexpr double k_value_1024{1024.0};
constexpr double k_value_513{513.0};
constexpr double k_value_2585{2585.0};
constexpr double k_value_10_25{10.25};

[[nodiscard]] yaha::Rs485SerialMessage decodeMessage(const std::vector<std::uint8_t>& bytes) {
    yaha::Rs485SerialMessage decoded{};
    yaha::decodeRs485SerialMessage(bytes, 0U, decoded);
    return decoded;
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("rs485_codec_roundtrip_version0_preserves_fields", "[rs485_protocol]") {
    yaha::Rs485SerialMessage message{};
    message.sender = k_addr_2;
    message.receiver = k_addr_6;
    message.reply = true;
    message.command = 'A';
    message.value = k_value_1024;
    message.version = 0U;

    const std::vector<std::uint8_t> encoded = yaha::encodeRs485SerialMessage(message);
    REQUIRE(encoded.size() == 7U);

    const yaha::Rs485SerialMessage decoded = decodeMessage(encoded);
    REQUIRE(decoded.sender == k_addr_2);
    REQUIRE(decoded.receiver == k_addr_6);
    REQUIRE(decoded.reply);
    REQUIRE(decoded.command == 'A');
    REQUIRE(decoded.version == 0U);
    REQUIRE(decoded.length == 7U);
    REQUIRE(std::fabs(decoded.value - k_value_1024) < k_comparison_epsilon);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("rs485_codec_roundtrip_version1_preserves_fields", "[rs485_protocol]") {
    yaha::Rs485SerialMessage message{};
    message.sender = k_addr_7;
    message.receiver = k_addr_3;
    message.reply = false;
    message.command = 'P';
    message.value = k_value_513;
    message.version = 1U;

    const std::vector<std::uint8_t> encoded = yaha::encodeRs485SerialMessage(message);
    REQUIRE(encoded.size() == 9U);

    const yaha::Rs485SerialMessage decoded = decodeMessage(encoded);
    REQUIRE(decoded.sender == k_addr_7);
    REQUIRE(decoded.receiver == k_addr_3);
    REQUIRE(decoded.reply == false);
    REQUIRE(decoded.command == 'P');
    REQUIRE(decoded.version == 1U);
    REQUIRE(decoded.length == 9U);
    REQUIRE(std::fabs(decoded.value - k_value_513) < k_comparison_epsilon);
}

TEST_CASE("rs485_codec_decodes_fractional_commands_h_t_s", "[rs485_protocol]") {
    yaha::Rs485SerialMessage message{};
    message.sender = 1U;
    message.receiver = 2U;
    message.reply = false;
    message.command = 'h';
    message.value = k_value_2585;
    message.version = 1U;

    const std::vector<std::uint8_t> encoded = yaha::encodeRs485SerialMessage(message);
    const yaha::Rs485SerialMessage decoded = decodeMessage(encoded);

    REQUIRE(std::fabs(decoded.value - k_value_10_25) < k_comparison_epsilon);
}

TEST_CASE("rs485_codec_rejects_crc_mismatch", "[rs485_protocol]") {
    yaha::Rs485SerialMessage message{};
    message.sender = k_addr_2;
    message.receiver = k_addr_3;
    message.reply = false;
    message.command = 'B';
    message.value = k_value_900;
    message.version = 1U;

    std::vector<std::uint8_t> encoded = yaha::encodeRs485SerialMessage(message);
    encoded[k_crc_high_index] ^= k_corrupt_crc_mask;

    yaha::Rs485SerialMessage decoded{};
    REQUIRE_THROWS_WITH(
        yaha::decodeRs485SerialMessage(encoded, 0U, decoded),
        Catch::Matchers::ContainsSubstring("CRC does not match"));
}

TEST_CASE("rs485_codec_rejects_unsupported_version_encode", "[rs485_protocol]") {
    yaha::Rs485SerialMessage message{};
    message.version = 2U;
    message.command = 'X';
    message.value = 1.0;

    REQUIRE_THROWS_WITH(
        yaha::encodeRs485SerialMessage(message),
        Catch::Matchers::ContainsSubstring("Unsupported message version"));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("rs485_stream_reader_skips_noise_and_parses_multiple_messages", "[rs485_protocol]") {
    yaha::Rs485SerialMessage first{};
    first.sender = k_addr_3;
    first.receiver = k_addr_8;
    first.command = 'C';
    first.value = k_value_33;
    first.version = 0U;

    yaha::Rs485SerialMessage second{};
    second.sender = k_addr_4;
    second.receiver = k_addr_9;
    second.command = 'D';
    second.value = k_value_66;
    second.version = 1U;

    std::vector<std::uint8_t> stream{0U, k_invalid_noise_byte};
    const auto firstBytes = yaha::encodeRs485SerialMessage(first);
    const auto secondBytes = yaha::encodeRs485SerialMessage(second);
    stream.insert(stream.end(), firstBytes.begin(), firstBytes.end());
    stream.insert(stream.end(), secondBytes.begin(), secondBytes.end());

    const auto results = yaha::Rs485StreamReader::read(stream);

    REQUIRE(results.size() == 2U);
    REQUIRE(results[0].error.empty());
    REQUIRE(results[0].message.has_value());
    REQUIRE(results[0].message->command == 'C');
    REQUIRE(results[1].error.empty());
    REQUIRE(results[1].message.has_value());
    REQUIRE(results[1].message->command == 'D');
}

TEST_CASE("rs485_stream_reader_reports_error_and_continues_by_message_length", "[rs485_protocol]") {
    yaha::Rs485SerialMessage broken{};
    broken.sender = k_addr_5;
    broken.receiver = k_addr_6;
    broken.command = 'E';
    broken.value = k_value_88;
    broken.version = 1U;

    std::vector<std::uint8_t> brokenBytes = yaha::encodeRs485SerialMessage(broken);
    brokenBytes[k_crc_low_index] ^= k_corrupt_crc_mask;

    yaha::Rs485SerialMessage valid{};
    valid.sender = k_addr_7;
    valid.receiver = k_addr_8;
    valid.command = 'F';
    valid.value = k_value_11;
    valid.version = 0U;

    const std::vector<std::uint8_t> validBytes = yaha::encodeRs485SerialMessage(valid);

    std::vector<std::uint8_t> stream = brokenBytes;
    stream.insert(stream.end(), validBytes.begin(), validBytes.end());

    const auto results = yaha::Rs485StreamReader::read(stream);

    REQUIRE(results.size() == 2U);
    REQUIRE(results[0].message.has_value() == false);
    REQUIRE(results[0].error.empty() == false);
    REQUIRE(results[1].message.has_value());
    REQUIRE(results[1].message->command == 'F');
}
