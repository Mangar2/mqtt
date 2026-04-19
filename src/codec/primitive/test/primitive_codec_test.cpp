#include <catch2/catch_test_macros.hpp>

#include "codec/primitive/primitive_codec.h"
#include <vector>

using namespace mqtt;

//  Helpers
//

static ReadBuffer make_reader(const std::vector<uint8_t> &bytes) {
  return ReadBuffer{std::span<const uint8_t>{bytes.data(), bytes.size()}};
}

//  ReadBuffer
//

TEST_CASE("read_buffer_position", "[primitive]") {
  std::vector<uint8_t> data{0x01U, 0x02U, 0x03U};
  auto reader = make_reader(data);
  CHECK(reader.position() == 0U);
  (void)reader.read_byte();
  CHECK(reader.position() == 1U);
  (void)reader.read_bytes(2);
  CHECK(reader.position() == 3U);
}

TEST_CASE("read_buffer_has_remaining", "[primitive]") {
  std::vector<uint8_t> data{0xAAU, 0xBBU};
  auto reader = make_reader(data);
  CHECK(reader.has_remaining(1));
  CHECK(reader.has_remaining(2));
  CHECK_FALSE(reader.has_remaining(3));
  (void)reader.read_byte();
  CHECK(reader.has_remaining(1));
  CHECK_FALSE(reader.has_remaining(2));
}

//  encode_byte / decode_byte
//

TEST_CASE("byte_encode", "[primitive]") {
  WriteBuffer buf;
  encode_byte(buf, 0x42U);
  CHECK(buf == std::vector<uint8_t>{0x42U});
}

TEST_CASE("byte_decode", "[primitive]") {
  std::vector<uint8_t> data{0xABU};
  auto reader = make_reader(data);
  CHECK(decode_byte(reader) == 0xABU);
}

TEST_CASE("byte_decode_empty", "[primitive]") {
  std::vector<uint8_t> data;
  auto reader = make_reader(data);
  CHECK_THROWS_AS(decode_byte(reader), CodecException);
  auto reader2 = make_reader(data);
  try {
    (void)decode_byte(reader2);
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::BufferTooShort);
  }
}

//  Variable Byte Integer
//

TEST_CASE("vbi_encode_1byte", "[primitive][vbi]") {
  WriteBuffer buf;
  encode_variable_byte_integer(buf, VariableByteInteger{0});
  CHECK(buf == std::vector<uint8_t>{0x00U});

  buf.clear();
  encode_variable_byte_integer(buf, VariableByteInteger{127U});
  CHECK(buf == std::vector<uint8_t>{0x7FU});
}

TEST_CASE("vbi_encode_2byte", "[primitive][vbi]") {
  WriteBuffer buf;
  encode_variable_byte_integer(buf, VariableByteInteger{128U});
  CHECK(buf == (std::vector<uint8_t>{0x80U, 0x01U}));

  buf.clear();
  encode_variable_byte_integer(buf, VariableByteInteger{16383U});
  CHECK(buf == (std::vector<uint8_t>{0xFFU, 0x7FU}));
}

TEST_CASE("vbi_encode_3byte", "[primitive][vbi]") {
  WriteBuffer buf;
  encode_variable_byte_integer(buf, VariableByteInteger{16384U});
  CHECK(buf == (std::vector<uint8_t>{0x80U, 0x80U, 0x01U}));

  buf.clear();
  encode_variable_byte_integer(buf, VariableByteInteger{2097151U});
  CHECK(buf == (std::vector<uint8_t>{0xFFU, 0xFFU, 0x7FU}));
}

TEST_CASE("vbi_encode_4byte", "[primitive][vbi]") {
  WriteBuffer buf;
  encode_variable_byte_integer(buf, VariableByteInteger{2097152U});
  CHECK(buf == (std::vector<uint8_t>{0x80U, 0x80U, 0x80U, 0x01U}));

  buf.clear();
  encode_variable_byte_integer(buf, VariableByteInteger{268435455U});
  CHECK(buf == (std::vector<uint8_t>{0xFFU, 0xFFU, 0xFFU, 0x7FU}));
}

TEST_CASE("vbi_encode_overflow", "[primitive][vbi]") {
  WriteBuffer buf;
  // value one above the maximum
  try {
    encode_variable_byte_integer(buf, VariableByteInteger{268435456U});
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::VariableByteIntegerOverflow);
  }
}

TEST_CASE("vbi_decode_1byte", "[primitive][vbi]") {
  SECTION("value 0") {
    std::vector<uint8_t> data{0x00U};
    auto reader = make_reader(data);
    CHECK(decode_variable_byte_integer(reader).value == 0U);
  }
  SECTION("value 127") {
    std::vector<uint8_t> data{0x7FU};
    auto reader = make_reader(data);
    CHECK(decode_variable_byte_integer(reader).value == 127U);
  }
}

TEST_CASE("vbi_decode_2byte", "[primitive][vbi]") {
  SECTION("value 128") {
    std::vector<uint8_t> data{0x80U, 0x01U};
    auto reader = make_reader(data);
    CHECK(decode_variable_byte_integer(reader).value == 128U);
  }
  SECTION("value 16383") {
    std::vector<uint8_t> data{0xFFU, 0x7FU};
    auto reader = make_reader(data);
    CHECK(decode_variable_byte_integer(reader).value == 16383U);
  }
}

TEST_CASE("vbi_decode_3byte", "[primitive][vbi]") {
  std::vector<uint8_t> data{0x80U, 0x80U, 0x01U};
  auto reader = make_reader(data);
  CHECK(decode_variable_byte_integer(reader).value == 16384U);
}

TEST_CASE("vbi_decode_4byte", "[primitive][vbi]") {
  std::vector<uint8_t> data{0xFFU, 0xFFU, 0xFFU, 0x7FU};
  auto reader = make_reader(data);
  CHECK(decode_variable_byte_integer(reader).value == 268435455U);
}

TEST_CASE("vbi_decode_truncated", "[primitive][vbi]") {
  std::vector<uint8_t> data{0x80U}; // continuation bit but no next byte
  auto reader = make_reader(data);
  try {
    (void)decode_variable_byte_integer(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::BufferTooShort);
  }
}

TEST_CASE("vbi_decode_overflow", "[primitive][vbi]") {
  std::vector<uint8_t> data{0xFFU, 0xFFU, 0xFFU,
                            0xFFU}; // continuation in byte 4
  auto reader = make_reader(data);
  try {
    (void)decode_variable_byte_integer(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::VariableByteIntegerOverflow);
  }
}

TEST_CASE("vbi_roundtrip", "[primitive][vbi]") {
  for (uint32_t value :
       {0U, 1U, 127U, 128U, 16383U, 16384U, 2097151U, 2097152U, 268435455U}) {
    WriteBuffer buf;
    encode_variable_byte_integer(buf, VariableByteInteger{value});
    auto reader = make_reader(buf);
    CHECK(decode_variable_byte_integer(reader).value == value);
  }
}

//  Two Byte Integer
//

TEST_CASE("two_byte_encode", "[primitive][integers]") {
  WriteBuffer buf;
  encode_two_byte_integer(buf, 0x1234U);
  CHECK(buf == (std::vector<uint8_t>{0x12U, 0x34U}));
}

TEST_CASE("two_byte_decode", "[primitive][integers]") {
  std::vector<uint8_t> data{0xBEU, 0xEFU};
  auto reader = make_reader(data);
  CHECK(decode_two_byte_integer(reader) == 0xBEEFU);
}

TEST_CASE("two_byte_decode_truncated", "[primitive][integers]") {
  std::vector<uint8_t> data{0x12U};
  auto reader = make_reader(data);
  try {
    (void)decode_two_byte_integer(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::BufferTooShort);
  }
}

TEST_CASE("two_byte_roundtrip", "[primitive][integers]") {
  WriteBuffer buf;
  encode_two_byte_integer(buf, 0xDEADU);
  auto reader = make_reader(buf);
  CHECK(decode_two_byte_integer(reader) == 0xDEADU);
}

//  Four Byte Integer
//

TEST_CASE("four_byte_encode", "[primitive][integers]") {
  WriteBuffer buf;
  encode_four_byte_integer(buf, 0x12345678U);
  CHECK(buf == (std::vector<uint8_t>{0x12U, 0x34U, 0x56U, 0x78U}));
}

TEST_CASE("four_byte_decode", "[primitive][integers]") {
  std::vector<uint8_t> data{0xDEU, 0xADU, 0xBEU, 0xEFU};
  auto reader = make_reader(data);
  CHECK(decode_four_byte_integer(reader) == 0xDEADBEEFU);
}

TEST_CASE("four_byte_decode_truncated", "[primitive][integers]") {
  std::vector<uint8_t> data{0x12U, 0x34U, 0x56U};
  auto reader = make_reader(data);
  try {
    (void)decode_four_byte_integer(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::BufferTooShort);
  }
}

TEST_CASE("four_byte_roundtrip", "[primitive][integers]") {
  WriteBuffer buf;
  encode_four_byte_integer(buf, 0xCAFEBABEU);
  auto reader = make_reader(buf);
  CHECK(decode_four_byte_integer(reader) == 0xCAFEBABEU);
}

//  UTF-8 String
//

TEST_CASE("utf8_encode_empty", "[primitive][utf8]") {
  WriteBuffer buf;
  encode_utf8_string(buf, Utf8String{""});
  CHECK(buf == (std::vector<uint8_t>{0x00U, 0x00U}));
}

TEST_CASE("utf8_encode", "[primitive][utf8]") {
  WriteBuffer buf;
  encode_utf8_string(buf, Utf8String{"hello"});
  CHECK(buf == (std::vector<uint8_t>{0x00U, 0x05U, 'h', 'e', 'l', 'l', 'o'}));
}

TEST_CASE("utf8_decode_empty", "[primitive][utf8]") {
  std::vector<uint8_t> data{0x00U, 0x00U};
  auto reader = make_reader(data);
  CHECK(decode_utf8_string(reader).value.empty());
}

TEST_CASE("utf8_decode", "[primitive][utf8]") {
  std::vector<uint8_t> data{0x00U, 0x03U, 'f', 'o', 'o'};
  auto reader = make_reader(data);
  CHECK(decode_utf8_string(reader).value == "foo");
}

TEST_CASE("utf8_decode_multibyte_valid", "[primitive][utf8]") {
  // U+20AC (3 bytes) + U+1F642 (4 bytes)
  std::vector<uint8_t> data{0x00U, 0x07U, 0xE2U, 0x82U, 0xACU,
                            0xF0U, 0x9FU, 0x99U, 0x82U};
  auto reader = make_reader(data);
  CHECK(decode_utf8_string(reader).value == "\u20AC\U0001F642");
}

TEST_CASE("utf8_decode_truncated", "[primitive][utf8]") {
  // length = 5 but only 2 bytes follow
  std::vector<uint8_t> data{0x00U, 0x05U, 'a', 'b'};
  auto reader = make_reader(data);
  try {
    (void)decode_utf8_string(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::BufferTooShort);
  }
}

TEST_CASE("utf8_decode_forbidden_null", "[primitive][utf8]") {
  std::vector<uint8_t> data{0x00U, 0x01U, 0x00U};
  auto reader = make_reader(data);
  try {
    (void)decode_utf8_string(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

TEST_CASE("utf8_decode_invalid_leading_byte", "[primitive][utf8]") {
  std::vector<uint8_t> data{0x00U, 0x01U, 0x80U};
  auto reader = make_reader(data);
  try {
    (void)decode_utf8_string(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

TEST_CASE("utf8_decode_invalid_continuation", "[primitive][utf8]") {
  // 0xC2 starts a 2-byte sequence; next byte must be 10xxxxxx.
  std::vector<uint8_t> data{0x00U, 0x02U, 0xC2U, 0x41U};
  auto reader = make_reader(data);
  try {
    (void)decode_utf8_string(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

TEST_CASE("utf8_decode_truncated_multibyte", "[primitive][utf8]") {
  // 0xE2 starts a 3-byte sequence but only one continuation is present.
  std::vector<uint8_t> data{0x00U, 0x02U, 0xE2U, 0x82U};
  auto reader = make_reader(data);
  try {
    (void)decode_utf8_string(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::MalformedPacket);
  }
}

TEST_CASE("utf8_roundtrip", "[primitive][utf8]") {
  WriteBuffer buf;
  encode_utf8_string(buf, Utf8String{"test"});
  auto reader = make_reader(buf);
  CHECK(decode_utf8_string(reader).value == "test");
}

//  UTF-8 String Pair
//

TEST_CASE("utf8_pair_roundtrip", "[primitive][utf8]") {
  WriteBuffer buf;
  Utf8StringPair original{Utf8String{"key"}, Utf8String{"val"}};
  encode_utf8_string_pair(buf, original);
  auto reader = make_reader(buf);
  CHECK(decode_utf8_string_pair(reader) == original);
}

//  Binary Data
//

TEST_CASE("binary_encode_empty", "[primitive][binary]") {
  WriteBuffer buf;
  encode_binary_data(buf, BinaryData{{}});
  CHECK(buf == (std::vector<uint8_t>{0x00U, 0x00U}));
}

TEST_CASE("binary_encode", "[primitive][binary]") {
  WriteBuffer buf;
  encode_binary_data(buf, BinaryData{{0x01U, 0x02U, 0x03U}});
  CHECK(buf == (std::vector<uint8_t>{0x00U, 0x03U, 0x01U, 0x02U, 0x03U}));
}

TEST_CASE("binary_decode", "[primitive][binary]") {
  std::vector<uint8_t> data{0x00U, 0x02U, 0xABU, 0xCDU};
  auto reader = make_reader(data);
  CHECK(decode_binary_data(reader) == BinaryData{{0xABU, 0xCDU}});
}

TEST_CASE("binary_decode_truncated", "[primitive][binary]") {
  std::vector<uint8_t> data{0x00U, 0x04U,
                            0x01U}; // says 4 bytes, only 1 available
  auto reader = make_reader(data);
  try {
    (void)decode_binary_data(reader);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::BufferTooShort);
  }
}

TEST_CASE("binary_roundtrip", "[primitive][binary]") {
  WriteBuffer buf;
  BinaryData original{{0xDEU, 0xADU}};
  encode_binary_data(buf, original);
  auto reader = make_reader(buf);
  CHECK(decode_binary_data(reader) == original);
}
