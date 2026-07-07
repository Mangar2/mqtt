#pragma once

/**
 * @file property_maps.h
 * @brief MQTT 5.0 property data-type and packet-allowed mappings (Module 1.3.2 / 1.3.3).
 */

#include <cstdint>
#include "property_id.h"
#include "data_model/packet/packet_type.h"

namespace mqtt {

/**
 * @brief Data type categories for MQTT 5.0 property values (Module 1.3.2).
 */
enum class PropertyDataType : uint8_t {
    Byte,
    TwoByteInteger,
    FourByteInteger,
    VariableByteInteger,
    Utf8String,
    Utf8StringPair,
    BinaryData,
};

/**
 * @brief Returns the wire data type mandated by the MQTT 5.0 spec for the given property ID.
 * @param id A valid PropertyId enumerator.
 * @return The PropertyDataType associated with @p id.
 *
 * Behaviour is well-defined for every valid PropertyId enumerator.
 */
[[nodiscard]] constexpr PropertyDataType property_data_type(PropertyId id) noexcept
{
    switch (id) {
        case PropertyId::PayloadFormatIndicator:          return PropertyDataType::Byte;
        case PropertyId::MessageExpiryInterval:           return PropertyDataType::FourByteInteger;
        case PropertyId::ContentType:                     return PropertyDataType::Utf8String;
        case PropertyId::ResponseTopic:                   return PropertyDataType::Utf8String;
        case PropertyId::CorrelationData:                 return PropertyDataType::BinaryData;
        case PropertyId::SubscriptionIdentifier:          return PropertyDataType::VariableByteInteger;
        case PropertyId::SessionExpiryInterval:           return PropertyDataType::FourByteInteger;
        case PropertyId::AssignedClientIdentifier:        return PropertyDataType::Utf8String;
        case PropertyId::ServerKeepAlive:                 return PropertyDataType::TwoByteInteger;
        case PropertyId::AuthenticationMethod:            return PropertyDataType::Utf8String;
        case PropertyId::AuthenticationData:              return PropertyDataType::BinaryData;
        case PropertyId::RequestProblemInformation:       return PropertyDataType::Byte;
        case PropertyId::WillDelayInterval:               return PropertyDataType::FourByteInteger;
        case PropertyId::RequestResponseInformation:      return PropertyDataType::Byte;
        case PropertyId::ResponseInformation:             return PropertyDataType::Utf8String;
        case PropertyId::ServerReference:                 return PropertyDataType::Utf8String;
        case PropertyId::ReasonString:                    return PropertyDataType::Utf8String;
        case PropertyId::ReceiveMaximum:                  return PropertyDataType::TwoByteInteger;
        case PropertyId::TopicAliasMaximum:               return PropertyDataType::TwoByteInteger;
        case PropertyId::TopicAlias:                      return PropertyDataType::TwoByteInteger;
        case PropertyId::MaximumQoS:                      return PropertyDataType::Byte;
        case PropertyId::RetainAvailable:                 return PropertyDataType::Byte;
        case PropertyId::UserProperty:                    return PropertyDataType::Utf8StringPair;
        case PropertyId::MaximumPacketSize:               return PropertyDataType::FourByteInteger;
        case PropertyId::WildcardSubscriptionAvailable:   return PropertyDataType::Byte;
        case PropertyId::SubscriptionIdentifierAvailable: return PropertyDataType::Byte;
        case PropertyId::SharedSubscriptionAvailable:     return PropertyDataType::Byte;
    }
    return PropertyDataType::Byte; // unreachable — satisfies non-void return
}

/**
 * @brief Returns true when the spec allows @p prop to appear in a @p pkt packet.
 * @param prop Property identifier to check.
 * @param pkt  Packet type to check against; use PacketType::Will for will properties.
 * @return `true` if the property is permitted in the given packet type.
 */
[[nodiscard]] constexpr bool is_property_allowed(PropertyId prop, PacketType pkt) noexcept
{
    switch (prop) {
        case PropertyId::PayloadFormatIndicator:
            return pkt == PacketType::Publish || pkt == PacketType::Will;
        case PropertyId::MessageExpiryInterval:
            return pkt == PacketType::Publish || pkt == PacketType::Will;
        case PropertyId::ContentType:
            return pkt == PacketType::Publish || pkt == PacketType::Will;
        case PropertyId::ResponseTopic:
            return pkt == PacketType::Publish || pkt == PacketType::Will;
        case PropertyId::CorrelationData:
            return pkt == PacketType::Publish || pkt == PacketType::Will;
        case PropertyId::SubscriptionIdentifier:
            return pkt == PacketType::Publish || pkt == PacketType::Subscribe;
        case PropertyId::SessionExpiryInterval:
            return pkt == PacketType::Connect  || pkt == PacketType::Connack
                || pkt == PacketType::Disconnect;
        case PropertyId::AssignedClientIdentifier:
            return pkt == PacketType::Connack;
        case PropertyId::ServerKeepAlive:
            return pkt == PacketType::Connack;
        case PropertyId::AuthenticationMethod:
            return pkt == PacketType::Connect || pkt == PacketType::Connack
                || pkt == PacketType::Auth;
        case PropertyId::AuthenticationData:
            return pkt == PacketType::Connect || pkt == PacketType::Connack
                || pkt == PacketType::Auth;
        case PropertyId::RequestProblemInformation:
            return pkt == PacketType::Connect;
        case PropertyId::WillDelayInterval:
            return pkt == PacketType::Will;
        case PropertyId::RequestResponseInformation:
            return pkt == PacketType::Connect;
        case PropertyId::ResponseInformation:
            return pkt == PacketType::Connack;
        case PropertyId::ServerReference:
            return pkt == PacketType::Connack || pkt == PacketType::Disconnect;
        case PropertyId::ReasonString:
            return pkt == PacketType::Connack  || pkt == PacketType::Puback
                || pkt == PacketType::Pubrec   || pkt == PacketType::Pubrel
                || pkt == PacketType::Pubcomp  || pkt == PacketType::Suback
                || pkt == PacketType::Unsuback || pkt == PacketType::Disconnect
                || pkt == PacketType::Auth;
        case PropertyId::ReceiveMaximum:
            return pkt == PacketType::Connect || pkt == PacketType::Connack;
        case PropertyId::TopicAliasMaximum:
            return pkt == PacketType::Connect || pkt == PacketType::Connack;
        case PropertyId::TopicAlias:
            return pkt == PacketType::Publish;
        case PropertyId::MaximumQoS:
            return pkt == PacketType::Connack;
        case PropertyId::RetainAvailable:
            return pkt == PacketType::Connack;
        case PropertyId::UserProperty:
            return pkt == PacketType::Connect    || pkt == PacketType::Connack
                || pkt == PacketType::Publish    || pkt == PacketType::Will
                || pkt == PacketType::Puback     || pkt == PacketType::Pubrec
                || pkt == PacketType::Pubrel     || pkt == PacketType::Pubcomp
                || pkt == PacketType::Subscribe  || pkt == PacketType::Suback
                || pkt == PacketType::Unsubscribe|| pkt == PacketType::Unsuback
                || pkt == PacketType::Disconnect || pkt == PacketType::Auth;
        case PropertyId::MaximumPacketSize:
            return pkt == PacketType::Connect || pkt == PacketType::Connack;
        case PropertyId::WildcardSubscriptionAvailable:
            return pkt == PacketType::Connack;
        case PropertyId::SubscriptionIdentifierAvailable:
            return pkt == PacketType::Connack;
        case PropertyId::SharedSubscriptionAvailable:
            return pkt == PacketType::Connack;
    }
    return false; // unreachable — satisfies non-void return
}

} // namespace mqtt
