#include "codec/properties/properties_codec.h"

#include "codec/primitive/primitive_codec.h"
#include "data_model/property/property_maps.h"

namespace mqtt {

namespace {

//  Type-match helper

[[nodiscard]] bool value_type_matches(const PropertyValue &value,
                                      PropertyDataType expected) noexcept {
  switch (expected) {
  case PropertyDataType::Byte:
    return std::holds_alternative<uint8_t>(value);
  case PropertyDataType::TwoByteInteger:
    return std::holds_alternative<TwoByteInteger>(value);
  case PropertyDataType::FourByteInteger:
    return std::holds_alternative<FourByteInteger>(value);
  case PropertyDataType::VariableByteInteger:
    return std::holds_alternative<VariableByteInteger>(value);
  case PropertyDataType::Utf8String:
    return std::holds_alternative<Utf8String>(value);
  case PropertyDataType::Utf8StringPair:
    return std::holds_alternative<Utf8StringPair>(value);
  case PropertyDataType::BinaryData:
    return std::holds_alternative<BinaryData>(value);
  }
  return false; // unreachable — satisfies non-void return
}

//  Property-ID validator (decode)

[[nodiscard]] PropertyId to_property_id(uint8_t byte) {
  switch (byte) {
  case 0x01U:
    return PropertyId::PayloadFormatIndicator;
  case 0x02U:
    return PropertyId::MessageExpiryInterval;
  case 0x03U:
    return PropertyId::ContentType;
  case 0x08U:
    return PropertyId::ResponseTopic;
  case 0x09U:
    return PropertyId::CorrelationData;
  case 0x0BU:
    return PropertyId::SubscriptionIdentifier;
  case 0x11U:
    return PropertyId::SessionExpiryInterval;
  case 0x12U:
    return PropertyId::AssignedClientIdentifier;
  case 0x13U:
    return PropertyId::ServerKeepAlive;
  case 0x15U:
    return PropertyId::AuthenticationMethod;
  case 0x16U:
    return PropertyId::AuthenticationData;
  case 0x17U:
    return PropertyId::RequestProblemInformation;
  case 0x18U:
    return PropertyId::WillDelayInterval;
  case 0x19U:
    return PropertyId::RequestResponseInformation;
  case 0x1AU:
    return PropertyId::ResponseInformation;
  case 0x1CU:
    return PropertyId::ServerReference;
  case 0x1FU:
    return PropertyId::ReasonString;
  case 0x21U:
    return PropertyId::ReceiveMaximum;
  case 0x22U:
    return PropertyId::TopicAliasMaximum;
  case 0x23U:
    return PropertyId::TopicAlias;
  case 0x24U:
    return PropertyId::MaximumQoS;
  case 0x25U:
    return PropertyId::RetainAvailable;
  case 0x26U:
    return PropertyId::UserProperty;
  case 0x27U:
    return PropertyId::MaximumPacketSize;
  case 0x28U:
    return PropertyId::WildcardSubscriptionAvailable;
  case 0x29U:
    return PropertyId::SubscriptionIdentifierAvailable;
  case 0x2AU:
    return PropertyId::SharedSubscriptionAvailable;
  default:
    throw CodecException{CodecError::InvalidPropertyId,
                         "Unknown MQTT property ID"};
  }
}

//  Property-value encoder helper

void encode_property_value(WriteBuffer &buf, const PropertyValue &value) {
  std::visit(
      [&buf](const auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, uint8_t>) {
          encode_byte(buf, v);
        } else if constexpr (std::is_same_v<T, TwoByteInteger>) {
          encode_two_byte_integer(buf, v);
        } else if constexpr (std::is_same_v<T, FourByteInteger>) {
          encode_four_byte_integer(buf, v);
        } else if constexpr (std::is_same_v<T, VariableByteInteger>) {
          encode_variable_byte_integer(buf, v);
        } else if constexpr (std::is_same_v<T, Utf8String>) {
          encode_utf8_string(buf, v);
        } else if constexpr (std::is_same_v<T, Utf8StringPair>) {
          encode_utf8_string_pair(buf, v);
        } else if constexpr (std::is_same_v<T, BinaryData>) {
          encode_binary_data(buf, v);
        }
      },
      value);
}

//  Property-value decoder helper

[[nodiscard]] PropertyValue decode_property_value(ReadBuffer &buf,
                                                  PropertyId id) {
  switch (property_data_type(id)) {
  case PropertyDataType::Byte:
    return decode_byte(buf);
  case PropertyDataType::TwoByteInteger:
    return decode_two_byte_integer(buf);
  case PropertyDataType::FourByteInteger:
    return decode_four_byte_integer(buf);
  case PropertyDataType::VariableByteInteger:
    return decode_variable_byte_integer(buf);
  case PropertyDataType::Utf8String:
    return decode_utf8_string(buf);
  case PropertyDataType::Utf8StringPair:
    return decode_utf8_string_pair(buf);
  case PropertyDataType::BinaryData:
    return decode_binary_data(buf);
  }
  return uint8_t{0}; // unreachable — satisfies non-void return
}

//  Duplicate-detection helpers

[[nodiscard]] bool is_seen(const std::vector<PropertyId> &seen,
                           PropertyId id) noexcept {
  for (const auto &s : seen) {
    if (s == id) {
      return true;
    }
  }
  return false;
}

} // anonymous namespace

//  Public API

void encode_properties(WriteBuffer &buf, const std::vector<Property> &props,
                       PacketType context) {
  // --- Validation pass ---
  std::vector<PropertyId> seen_non_repeatable;
  for (const auto &prop : props) {
    // 1. Type match
    if (!value_type_matches(prop.value, property_data_type(prop.id))) {
      throw CodecException{
          CodecError::PropertyTypeMismatch,
          "Property value type does not match the expected type for its ID"};
    }
    // 2. Allowed in context
    if (!is_property_allowed(prop.id, context)) {
      throw CodecException{CodecError::PropertyNotAllowed,
                           "Property is not allowed in the given packet type"};
    }
    // 3. Duplicate detection (UserProperty may repeat)
    if (prop.id != PropertyId::UserProperty) {
      if (is_seen(seen_non_repeatable, prop.id)) {
        throw CodecException{CodecError::DuplicateProperty,
                             "Non-repeatable property appears more than once"};
      }
      seen_non_repeatable.push_back(prop.id);
    }
  }

  // --- Encode into temporary buffer to determine total byte length ---
  WriteBuffer temp;
  for (const auto &prop : props) {
    temp.push_back(static_cast<uint8_t>(prop.id));
    encode_property_value(temp, prop.value);
  }

  // --- Write VBI length prefix + property bytes ---
  encode_variable_byte_integer(
      buf, VariableByteInteger{static_cast<uint32_t>(temp.size())});
  buf.insert(buf.end(), temp.begin(), temp.end());
}

std::vector<Property> decode_properties(ReadBuffer &buf, PacketType context) {
  VariableByteInteger length_vbi = decode_variable_byte_integer(buf);
  const uint32_t props_length = length_vbi.value;

  if (props_length == 0U) {
    return {};
  }

  auto props_span = buf.read_bytes(props_length);
  ReadBuffer props_buf{props_span};

  std::vector<Property> result;
  std::vector<PropertyId> seen_non_repeatable;

  while (props_buf.remaining() > 0) {
    uint8_t id_byte = decode_byte(props_buf);
    PropertyId id = to_property_id(id_byte);

    // Allowed in context
    if (!is_property_allowed(id, context)) {
      throw CodecException{CodecError::PropertyNotAllowed,
                           "Property is not allowed in the given packet type"};
    }

    // Duplicate detection
    if (id != PropertyId::UserProperty) {
      if (is_seen(seen_non_repeatable, id)) {
        throw CodecException{CodecError::DuplicateProperty,
                             "Non-repeatable property appears more than once"};
      }
      seen_non_repeatable.push_back(id);
    }

    PropertyValue value = decode_property_value(props_buf, id);
    result.push_back(Property{id, std::move(value)});
  }

  return result;
}

} // namespace mqtt
