#include <catch2/catch_test_macros.hpp>

#include "yaha/rs485_protocol/rs485_serial_protocol.h"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double k_comparison_epsilon{1e-9};
constexpr std::uint8_t k_corrupt_crc_mask{0xFFU};

[[nodiscard]] yaha::Rs485SerialMessage decodeMessage(const std::vector<std::uint8_t>& bytes) {
    yaha::Rs485SerialMessage decoded{};
    yaha::decodeRs485SerialMessage(bytes, 0U, decoded);
    return decoded;
}

} // namespace

TEST_CASE("rs485_codec_roundtrip_version0_preserves_fields", "[rs485_protocol]") {
    yaha::Rs485SerialMessage message{};
    message.sender = 2U;
    message.receiver = 6U;
    message.reply = true;
    message.command = 'A';
    message.value = 1024.0;
    message.version = 0U;

    const std::vector<std::uint8_t> encoded = yaha::encodeRs485SerialMessage(message);
    REQUIRE(encoded.size() == 7U);

    const yaha::Rs485SerialMessage decoded = decodeMessage(encoded);
    REQUIRE(decoded.sender == 2U);
    REQUIRE(decoded.receiver == 6U);
    REQUIRE(decoded.reply);
    REQUIRE(decoded.command == 'A');
    REQUIRE(decoded.version == 0U);
    REQUIRE(decoded.length == 7U);
    REQUIRE(std::fabs(decoded.value - 1024.0) < k_comparison_epsilon);
}

TEST_CASE("rs485_codec_roundtrip_version1_preserves_fields", "[rs485_protocol]") {
    yaha::Rs485SerialMessage message{};
    message.sender = 7U;
    message.receiver = 3U;
    message.reply = false;
    message.command = 'P';
    message.value = 513.0;
    message.version = 1U;

    const std::vector<std::uint8_t> encoded = yaha::encodeRs485SerialMessage(message);
    REQUIRE(encoded.size() == 9U);

    const yaha::Rs485SerialMessage decoded = decodeMessage(encoded);
    REQUIRE(decoded.sender == 7U);
    REQUIRE(decoded.receiver == 3U);
    REQUIRE(decoded.reply == false);
    REQUIRE(decoded.command == 'P');
    REQUIRE(decoded.version == 1U);
    REQUIRE(decoded.length == 9U);
    REQUIRE(std::fabs(decoded.value - 513.0) < k_comparison_epsilon);
}

TEST_CASE("rs485_codec_decodes_fractional_commands_h_t_s", "[rs485_protocol]") {
    yaha::Rs485SerialMessage message{};
    message.sender = 1U;
    message.receiver = 2U;
    message.reply = false;
    message.command = 'h';
    message.value = 2585.0;
    message.version = 1U;

    const std::vector<std::uint8_t> encoded = yaha::encodeRs485SerialMessage(message);
    const yaha::Rs485SerialMessage decoded = decodeMessage(encoded);

    REQUIRE(std::fabs(decoded.value - 10.25) < k_comparison_epsilon);
}

TEST_CASE("rs485_codec_rejects_crc_mismatch", "[rs485_protocol]") {
    yaha::Rs485SerialMessage message{};
    message.sender = 2U;
    message.receiver = 3U;
    message.reply = false;
    message.command = 'B';
    message.value = 900.0;
    message.version = 1U;

    std::vector<std::uint8_t> encoded = yaha::encodeRs485SerialMessage(message);
    encoded[8] ^= k_corrupt_crc_mask;

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

TEST_CASE("rs485_stream_reader_skips_noise_and_parses_multiple_messages", "[rs485_protocol]") {
    yaha::Rs485SerialMessage first{};
    first.sender = 3U;
    first.receiver = 8U;
    first.command = 'C';
    first.value = 33.0;
    first.version = 0U;

    yaha::Rs485SerialMessage second{};
    second.sender = 4U;
    second.receiver = 9U;
    second.command = 'D';
    second.value = 66.0;
    second.version = 1U;

    std::vector<std::uint8_t> stream{0U, 255U};
    const auto firstBytes = yaha::encodeRs485SerialMessage(first);
    const auto secondBytes = yaha::encodeRs485SerialMessage(second);
    stream.insert(stream.end(), firstBytes.begin(), firstBytes.end());
    stream.insert(stream.end(), secondBytes.begin(), secondBytes.end());

    yaha::Rs485StreamReader reader{};
    const auto results = reader.read(stream);

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
    broken.sender = 5U;
    broken.receiver = 6U;
    broken.command = 'E';
    broken.value = 88.0;
    broken.version = 1U;

    std::vector<std::uint8_t> brokenBytes = yaha::encodeRs485SerialMessage(broken);
    brokenBytes[7] ^= k_corrupt_crc_mask;

    yaha::Rs485SerialMessage valid{};
    valid.sender = 7U;
    valid.receiver = 8U;
    valid.command = 'F';
    valid.value = 11.0;
    valid.version = 0U;

    const std::vector<std::uint8_t> validBytes = yaha::encodeRs485SerialMessage(valid);

    std::vector<std::uint8_t> stream = brokenBytes;
    stream.insert(stream.end(), validBytes.begin(), validBytes.end());

    yaha::Rs485StreamReader reader{};
    const auto results = reader.read(stream);

    REQUIRE(results.size() == 2U);
    REQUIRE(results[0].message.has_value() == false);
    REQUIRE(results[0].error.empty() == false);
    REQUIRE(results[1].message.has_value());
    REQUIRE(results[1].message->command == 'F');
}
