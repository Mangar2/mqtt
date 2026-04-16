#include <catch2/catch_test_macros.hpp>

#include "data_model/property/property_id.h"
#include "data_model/property/property.h"
#include "data_model/property/property_maps.h"
#include "data_model/packet/packet_type.h"

using namespace mqtt;

// ── PropertyId values (1.3.1) ─────────────────────────────────────────────────

TEST_CASE("prop_id_payload_format", "[property]")
{
    STATIC_CHECK(static_cast<uint8_t>(PropertyId::PayloadFormatIndicator) == 0x01U);
}

TEST_CASE("prop_id_user_property", "[property]")
{
    STATIC_CHECK(static_cast<uint8_t>(PropertyId::UserProperty) == 0x26U);
}

TEST_CASE("prop_id_shared_sub_available", "[property]")
{
    STATIC_CHECK(static_cast<uint8_t>(PropertyId::SharedSubscriptionAvailable) == 0x2AU);
}

// ── Property struct ───────────────────────────────────────────────────────────

TEST_CASE("property_equality", "[property]")
{
    Property p1{PropertyId::MaximumQoS, uint8_t{2}};
    Property p2{PropertyId::MaximumQoS, uint8_t{2}};
    CHECK(p1 == p2);
}

TEST_CASE("property_inequality", "[property]")
{
    Property p1{PropertyId::MaximumQoS,      uint8_t{2}};
    Property p2{PropertyId::RetainAvailable, uint8_t{2}};
    CHECK(p1 != p2);
}

// ── property_data_type (1.3.2) ────────────────────────────────────────────────

TEST_CASE("prop_type_byte", "[property][data_type]")
{
    STATIC_CHECK(property_data_type(PropertyId::PayloadFormatIndicator)    == PropertyDataType::Byte);
    STATIC_CHECK(property_data_type(PropertyId::MaximumQoS)                == PropertyDataType::Byte);
    STATIC_CHECK(property_data_type(PropertyId::RetainAvailable)           == PropertyDataType::Byte);
    STATIC_CHECK(property_data_type(PropertyId::RequestProblemInformation) == PropertyDataType::Byte);
    STATIC_CHECK(property_data_type(PropertyId::RequestResponseInformation)== PropertyDataType::Byte);
    STATIC_CHECK(property_data_type(PropertyId::WildcardSubscriptionAvailable)   == PropertyDataType::Byte);
    STATIC_CHECK(property_data_type(PropertyId::SubscriptionIdentifierAvailable) == PropertyDataType::Byte);
    STATIC_CHECK(property_data_type(PropertyId::SharedSubscriptionAvailable)     == PropertyDataType::Byte);
}

TEST_CASE("prop_type_two_byte", "[property][data_type]")
{
    STATIC_CHECK(property_data_type(PropertyId::ServerKeepAlive)   == PropertyDataType::TwoByteInteger);
    STATIC_CHECK(property_data_type(PropertyId::ReceiveMaximum)    == PropertyDataType::TwoByteInteger);
    STATIC_CHECK(property_data_type(PropertyId::TopicAliasMaximum) == PropertyDataType::TwoByteInteger);
    STATIC_CHECK(property_data_type(PropertyId::TopicAlias)        == PropertyDataType::TwoByteInteger);
}

TEST_CASE("prop_type_four_byte", "[property][data_type]")
{
    STATIC_CHECK(property_data_type(PropertyId::MessageExpiryInterval)  == PropertyDataType::FourByteInteger);
    STATIC_CHECK(property_data_type(PropertyId::SessionExpiryInterval)  == PropertyDataType::FourByteInteger);
    STATIC_CHECK(property_data_type(PropertyId::WillDelayInterval)      == PropertyDataType::FourByteInteger);
    STATIC_CHECK(property_data_type(PropertyId::MaximumPacketSize)      == PropertyDataType::FourByteInteger);
}

TEST_CASE("prop_type_vbi", "[property][data_type]")
{
    STATIC_CHECK(property_data_type(PropertyId::SubscriptionIdentifier) == PropertyDataType::VariableByteInteger);
}

TEST_CASE("prop_type_utf8", "[property][data_type]")
{
    STATIC_CHECK(property_data_type(PropertyId::ContentType)              == PropertyDataType::Utf8String);
    STATIC_CHECK(property_data_type(PropertyId::ResponseTopic)            == PropertyDataType::Utf8String);
    STATIC_CHECK(property_data_type(PropertyId::AssignedClientIdentifier) == PropertyDataType::Utf8String);
    STATIC_CHECK(property_data_type(PropertyId::AuthenticationMethod)     == PropertyDataType::Utf8String);
    STATIC_CHECK(property_data_type(PropertyId::ResponseInformation)      == PropertyDataType::Utf8String);
    STATIC_CHECK(property_data_type(PropertyId::ServerReference)          == PropertyDataType::Utf8String);
    STATIC_CHECK(property_data_type(PropertyId::ReasonString)             == PropertyDataType::Utf8String);
}

TEST_CASE("prop_type_utf8_pair", "[property][data_type]")
{
    STATIC_CHECK(property_data_type(PropertyId::UserProperty) == PropertyDataType::Utf8StringPair);
}

TEST_CASE("prop_type_binary", "[property][data_type]")
{
    STATIC_CHECK(property_data_type(PropertyId::CorrelationData)    == PropertyDataType::BinaryData);
    STATIC_CHECK(property_data_type(PropertyId::AuthenticationData) == PropertyDataType::BinaryData);
}

// ── is_property_allowed (1.3.3) ───────────────────────────────────────────────

TEST_CASE("prop_allowed_payload_format_in_publish", "[property][allowed]")
{
    STATIC_CHECK( is_property_allowed(PropertyId::PayloadFormatIndicator, PacketType::Publish));
    STATIC_CHECK( is_property_allowed(PropertyId::PayloadFormatIndicator, PacketType::Will));
}

TEST_CASE("prop_allowed_payload_format_in_connect", "[property][allowed]")
{
    STATIC_CHECK(!is_property_allowed(PropertyId::PayloadFormatIndicator, PacketType::Connect));
    STATIC_CHECK(!is_property_allowed(PropertyId::PayloadFormatIndicator, PacketType::Subscribe));
}

TEST_CASE("prop_allowed_user_prop_everywhere", "[property][allowed]")
{
    STATIC_CHECK(is_property_allowed(PropertyId::UserProperty, PacketType::Connect));
    STATIC_CHECK(is_property_allowed(PropertyId::UserProperty, PacketType::Connack));
    STATIC_CHECK(is_property_allowed(PropertyId::UserProperty, PacketType::Publish));
    STATIC_CHECK(is_property_allowed(PropertyId::UserProperty, PacketType::Will));
    STATIC_CHECK(is_property_allowed(PropertyId::UserProperty, PacketType::Puback));
    STATIC_CHECK(is_property_allowed(PropertyId::UserProperty, PacketType::Subscribe));
    STATIC_CHECK(is_property_allowed(PropertyId::UserProperty, PacketType::Suback));
    STATIC_CHECK(is_property_allowed(PropertyId::UserProperty, PacketType::Unsubscribe));
    STATIC_CHECK(is_property_allowed(PropertyId::UserProperty, PacketType::Unsuback));
    STATIC_CHECK(is_property_allowed(PropertyId::UserProperty, PacketType::Disconnect));
    STATIC_CHECK(is_property_allowed(PropertyId::UserProperty, PacketType::Auth));
}

TEST_CASE("prop_allowed_topic_alias_only_publish", "[property][allowed]")
{
    STATIC_CHECK( is_property_allowed(PropertyId::TopicAlias, PacketType::Publish));
    STATIC_CHECK(!is_property_allowed(PropertyId::TopicAlias, PacketType::Subscribe));
    STATIC_CHECK(!is_property_allowed(PropertyId::TopicAlias, PacketType::Connect));
}

TEST_CASE("prop_allowed_will_delay_only_will", "[property][allowed]")
{
    STATIC_CHECK(!is_property_allowed(PropertyId::WillDelayInterval, PacketType::Publish));
    STATIC_CHECK(!is_property_allowed(PropertyId::WillDelayInterval, PacketType::Connect));
}

TEST_CASE("prop_allowed_will_delay_in_will", "[property][allowed]")
{
    STATIC_CHECK(is_property_allowed(PropertyId::WillDelayInterval, PacketType::Will));
}

TEST_CASE("prop_allowed_session_expiry_multi", "[property][allowed]")
{
    STATIC_CHECK(is_property_allowed(PropertyId::SessionExpiryInterval, PacketType::Connect));
    STATIC_CHECK(is_property_allowed(PropertyId::SessionExpiryInterval, PacketType::Connack));
    STATIC_CHECK(is_property_allowed(PropertyId::SessionExpiryInterval, PacketType::Disconnect));
    STATIC_CHECK(!is_property_allowed(PropertyId::SessionExpiryInterval, PacketType::Publish));
}
