#include <catch2/catch_test_macros.hpp>

#include "codec/primitive/primitive_codec.h"
#include "codec/properties/properties_codec.h"
#include <vector>

using namespace mqtt;

//  Helpers

static ReadBuffer make_reader(const std::vector<uint8_t> &bytes) {
  return ReadBuffer{std::span<const uint8_t>{bytes.data(), bytes.size()}};
}

//  encode_properties

TEST_CASE("props_encode_empty", "[properties]") {
  WriteBuffer buf;
  encode_properties(buf, {}, PacketType::Connect);
  CHECK(buf == std::vector<uint8_t>{0x00U}); // VBI length = 0
}

TEST_CASE("props_encode_byte_property", "[properties]") {
  WriteBuffer buf;
  std::vector<Property> props{{PropertyId::PayloadFormatIndicator, uint8_t{1}}};
  encode_properties(buf, props, PacketType::Publish);

  // Expected: VBI(2) + [0x01, 0x01]
  CHECK(buf.size() == 3U);
  CHECK(buf[0] == 0x02U); // length VBI
  CHECK(buf[1] == 0x01U); // property ID
  CHECK(buf[2] == 0x01U); // value
}

TEST_CASE("props_encode_two_byte_property", "[properties]") {
  WriteBuffer buf;
  std::vector<Property> props{
      {PropertyId::ReceiveMaximum, TwoByteInteger{0x0064U}}};
  encode_properties(buf, props, PacketType::Connect);

  CHECK(buf.size() == 4U);
  CHECK(buf[0] == 0x03U); // length VBI = 3 bytes
  CHECK(buf[1] == 0x21U); // ReceiveMaximum ID
  CHECK(buf[2] == 0x00U);
  CHECK(buf[3] == 0x64U); // 100 big-endian
}

TEST_CASE("props_encode_four_byte_property", "[properties]") {
  WriteBuffer buf;
  std::vector<Property> props{
      {PropertyId::SessionExpiryInterval, FourByteInteger{3600U}}};
  encode_properties(buf, props, PacketType::Connect);

  CHECK(buf.size() == 6U);
  CHECK(buf[0] == 0x05U); // length VBI = 5 bytes
  CHECK(buf[1] == 0x11U); // SessionExpiryInterval ID
}

TEST_CASE("props_encode_vbi_property", "[properties]") {
  WriteBuffer buf;
  std::vector<Property> props{
      {PropertyId::SubscriptionIdentifier, VariableByteInteger{10U}}};
  encode_properties(buf, props, PacketType::Subscribe);

  CHECK(buf.size() == 3U); // length VBI(2) + ID(1) + value VBI(1)
  CHECK(buf[0] == 0x02U);
  CHECK(buf[1] == 0x0BU); // SubscriptionIdentifier ID
  CHECK(buf[2] == 0x0AU); // value 10
}

TEST_CASE("props_encode_utf8_property", "[properties]") {
  WriteBuffer buf;
  std::vector<Property> props{
      {PropertyId::ContentType, Utf8String{"text/plain"}}};
  encode_properties(buf, props, PacketType::Publish);
  // length = 1(ID) + 2(len prefix) + 10(bytes) = 13
  CHECK(buf.size() == 14U); // VBI(1) + 13 bytes
}

TEST_CASE("props_encode_binary_property", "[properties]") {
  WriteBuffer buf;
  std::vector<Property> props{
      {PropertyId::CorrelationData, BinaryData{{0xAAU, 0xBBU}}}};
  encode_properties(buf, props, PacketType::Publish);
  // length = 1(ID) + 2(len prefix) + 2(bytes) = 5
  CHECK(buf.size() == 6U);
}

TEST_CASE("props_encode_multiple", "[properties]") {
  WriteBuffer buf;
  std::vector<Property> props{{PropertyId::PayloadFormatIndicator, uint8_t{1}},
                              {PropertyId::ContentType, Utf8String{"json"}}};
  encode_properties(buf, props, PacketType::Publish);
  // VBI(1) + 1+1 + 1+2+4 = VBI(1) + 9 bytes
  CHECK(buf.size() == 10U);
}

TEST_CASE("props_encode_type_mismatch", "[properties]") {
  WriteBuffer buf;
  // PayloadFormatIndicator expects uint8_t; give TwoByteInteger instead
  std::vector<Property> props{
      {PropertyId::PayloadFormatIndicator, TwoByteInteger{1U}}};
  try {
    encode_properties(buf, props, PacketType::Publish);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::PropertyTypeMismatch);
  }
}

TEST_CASE("props_encode_not_allowed", "[properties]") {
  WriteBuffer buf;
  // TopicAlias is only allowed in PUBLISH, not CONNECT
  std::vector<Property> props{{PropertyId::TopicAlias, TwoByteInteger{1U}}};
  try {
    encode_properties(buf, props, PacketType::Connect);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::PropertyNotAllowed);
  }
}

TEST_CASE("props_encode_duplicate", "[properties]") {
  WriteBuffer buf;
  std::vector<Property> props{{PropertyId::PayloadFormatIndicator, uint8_t{0}},
                              {PropertyId::PayloadFormatIndicator, uint8_t{1}}};
  try {
    encode_properties(buf, props, PacketType::Publish);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::DuplicateProperty);
  }
}

TEST_CASE("props_encode_user_prop_repeat", "[properties]") {
  WriteBuffer buf;
  std::vector<Property> props{
      {PropertyId::UserProperty,
       Utf8StringPair{Utf8String{"k1"}, Utf8String{"v1"}}},
      {PropertyId::UserProperty,
       Utf8StringPair{Utf8String{"k2"}, Utf8String{"v2"}}}};
  // Must NOT throw — UserProperty is repeatable
  CHECK_NOTHROW(encode_properties(buf, props, PacketType::Publish));
  CHECK(buf.size() > 1U);
}

//  decode_properties

TEST_CASE("props_decode_empty", "[properties]") {
  std::vector<uint8_t> data{0x00U};
  auto reader = make_reader(data);
  auto result = decode_properties(reader, PacketType::Connect);
  CHECK(result.empty());
}

TEST_CASE("props_decode_single", "[properties]") {
  // VBI(2) + [0x01, 0x01] (PayloadFormatIndicator = 1)
  std::vector<uint8_t> data{0x02U, 0x01U, 0x01U};
  auto reader = make_reader(data);
  auto result = decode_properties(reader, PacketType::Publish);
  REQUIRE(result.size() == 1U);
  CHECK(result[0].id == PropertyId::PayloadFormatIndicator);
  CHECK(std::get<uint8_t>(result[0].value) == 1U);
}

TEST_CASE("props_decode_multiple", "[properties]") {
  // PayloadFormatIndicator(1) + SessionExpiryInterval is not in Publish...
  // Use Publish context: PayloadFormatIndicator + ContentType
  WriteBuffer buf;
  std::vector<Property> props{{PropertyId::PayloadFormatIndicator, uint8_t{1}},
                              {PropertyId::ContentType, Utf8String{"json"}}};
  encode_properties(buf, props, PacketType::Publish);

  auto reader = make_reader(buf);
  auto result = decode_properties(reader, PacketType::Publish);
  REQUIRE(result.size() == 2U);
  CHECK(result[0].id == PropertyId::PayloadFormatIndicator);
  CHECK(result[1].id == PropertyId::ContentType);
  CHECK(std::get<Utf8String>(result[1].value).value == "json");
}

TEST_CASE("props_decode_invalid_id", "[properties]") {
  // VBI(2) + [0x00, 0x01] — property ID 0x00 is not a valid MQTT property
  std::vector<uint8_t> data{0x02U, 0x00U, 0x01U};
  auto reader = make_reader(data);
  try {
    (void)decode_properties(reader, PacketType::Publish);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::InvalidPropertyId);
  }
}

TEST_CASE("props_decode_not_allowed", "[properties]") {
  // TopicAlias (0x23) is only valid in PUBLISH, not CONNECT
  // VBI(3) + [0x23, 0x00, 0x01]
  std::vector<uint8_t> data{0x03U, 0x23U, 0x00U, 0x01U};
  auto reader = make_reader(data);
  try {
    (void)decode_properties(reader, PacketType::Connect);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::PropertyNotAllowed);
  }
}

TEST_CASE("props_decode_duplicate", "[properties]") {
  // Two PayloadFormatIndicator properties in one section
  // VBI(4) + [0x01,0x01,0x01,0x01]
  std::vector<uint8_t> data{0x04U, 0x01U, 0x01U, 0x01U, 0x01U};
  auto reader = make_reader(data);
  try {
    (void)decode_properties(reader, PacketType::Publish);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::DuplicateProperty);
  }
}

TEST_CASE("props_decode_truncated", "[properties]") {
  // VBI says 5 bytes but only 2 bytes follow
  std::vector<uint8_t> data{0x05U, 0x01U, 0x01U};
  auto reader = make_reader(data);
  try {
    (void)decode_properties(reader, PacketType::Publish);
    FAIL("Expected CodecException");
  } catch (const CodecException &e) {
    CHECK(e.error() == CodecError::BufferTooShort);
  }
}

TEST_CASE("props_decode_user_prop_repeat", "[properties]") {
  WriteBuffer buf;
  std::vector<Property> props{
      {PropertyId::UserProperty,
       Utf8StringPair{Utf8String{"a"}, Utf8String{"1"}}},
      {PropertyId::UserProperty,
       Utf8StringPair{Utf8String{"b"}, Utf8String{"2"}}}};
  encode_properties(buf, props, PacketType::Publish);

  auto reader = make_reader(buf);
  auto result = decode_properties(reader, PacketType::Publish);
  REQUIRE(result.size() == 2U);
  CHECK(result[0].id == PropertyId::UserProperty);
  CHECK(result[1].id == PropertyId::UserProperty);
}

TEST_CASE("props_decode_remaining_property_ids", "[properties]") {
  // Exercises all to_property_id() cases not covered by other decode tests.
  // Uses encode+decode roundtrip to avoid hand-crafting wire bytes.

  SECTION("connack properties") {
    std::vector<Property> props{
        {PropertyId::SessionExpiryInterval, FourByteInteger{300U}},
        {PropertyId::AssignedClientIdentifier, Utf8String{"client-42"}},
        {PropertyId::ServerKeepAlive, TwoByteInteger{60U}},
        {PropertyId::ResponseInformation, Utf8String{"/resp"}},
        {PropertyId::ServerReference, Utf8String{"other.broker"}},
        {PropertyId::ReasonString, Utf8String{"success"}},
        {PropertyId::ReceiveMaximum, TwoByteInteger{100U}},
        {PropertyId::TopicAliasMaximum, TwoByteInteger{10U}},
        {PropertyId::MaximumQoS, uint8_t{1U}},
        {PropertyId::RetainAvailable, uint8_t{1U}},
        {PropertyId::MaximumPacketSize, FourByteInteger{65535U}},
        {PropertyId::WildcardSubscriptionAvailable, uint8_t{1U}},
        {PropertyId::SubscriptionIdentifierAvailable, uint8_t{1U}},
        {PropertyId::SharedSubscriptionAvailable, uint8_t{0U}},
    };
    WriteBuffer buf;
    encode_properties(buf, props, PacketType::Connack);
    auto reader = make_reader(buf);
    auto result = decode_properties(reader, PacketType::Connack);
    REQUIRE(result.size() == props.size());
    for (std::size_t i = 0; i < props.size(); ++i) {
      CHECK(result[i].id == props[i].id);
    }
  }

  SECTION("connect properties") {
    std::vector<Property> props{
        {PropertyId::AuthenticationMethod, Utf8String{"SCRAM-SHA-256"}},
        {PropertyId::AuthenticationData, BinaryData{{0x01U, 0x02U}}},
        {PropertyId::RequestProblemInformation, uint8_t{1U}},
        {PropertyId::RequestResponseInformation, uint8_t{0U}},
    };
    WriteBuffer buf;
    encode_properties(buf, props, PacketType::Connect);
    auto reader = make_reader(buf);
    auto result = decode_properties(reader, PacketType::Connect);
    REQUIRE(result.size() == props.size());
    for (std::size_t i = 0; i < props.size(); ++i) {
      CHECK(result[i].id == props[i].id);
    }
  }

  SECTION("publish properties") {
    std::vector<Property> props{
        {PropertyId::ResponseTopic, Utf8String{"/response"}},
    };
    WriteBuffer buf;
    encode_properties(buf, props, PacketType::Publish);
    auto reader = make_reader(buf);
    auto result = decode_properties(reader, PacketType::Publish);
    REQUIRE(result.size() == 1U);
    CHECK(result[0].id == PropertyId::ResponseTopic);
  }

  SECTION("will properties") {
    std::vector<Property> props{
        {PropertyId::WillDelayInterval, FourByteInteger{30U}},
    };
    WriteBuffer buf;
    encode_properties(buf, props, PacketType::Will);
    auto reader = make_reader(buf);
    auto result = decode_properties(reader, PacketType::Will);
    REQUIRE(result.size() == 1U);
    CHECK(result[0].id == PropertyId::WillDelayInterval);
  }
}

//  Round-trip

TEST_CASE("props_roundtrip", "[properties]") {
  std::vector<Property> original{
      {PropertyId::PayloadFormatIndicator, uint8_t{1}},
      {PropertyId::MessageExpiryInterval, FourByteInteger{60U}},
      {PropertyId::ContentType, Utf8String{"application/json"}},
      {PropertyId::CorrelationData, BinaryData{{0x01U, 0x02U}}},
      {PropertyId::SubscriptionIdentifier, VariableByteInteger{42U}},
      {PropertyId::TopicAlias, TwoByteInteger{7U}},
      {PropertyId::UserProperty,
       Utf8StringPair{Utf8String{"key"}, Utf8String{"value"}}}};

  WriteBuffer buf;
  encode_properties(buf, original, PacketType::Publish);

  auto reader = make_reader(buf);
  auto decoded = decode_properties(reader, PacketType::Publish);

  REQUIRE(decoded.size() == original.size());
  for (std::size_t i = 0; i < original.size(); ++i) {
    CHECK(decoded[i] == original[i]);
  }
}
