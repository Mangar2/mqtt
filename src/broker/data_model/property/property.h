#pragma once

/**
 * @file property.h
 * @brief MQTT 5.0 property value variant and Property struct.
 */

#include <variant>
#include "property_id.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/integers.h"
#include "data_model/types/utf8_string.h"
#include "data_model/types/variable_byte_integer.h"

namespace mqtt {

/**
 * @brief Typed value of a single MQTT 5.0 property.
 *
 * The active alternative corresponds to the data type mandated by the property's ID
 * (see property_maps.h / property_data_type()).
 */
using PropertyValue = std::variant<
    uint8_t,              ///< Byte
    TwoByteInteger,       ///< Two Byte Integer  (uint16_t)
    FourByteInteger,      ///< Four Byte Integer (uint32_t)
    VariableByteInteger,  ///< Variable Byte Integer
    Utf8String,           ///< UTF-8 String
    Utf8StringPair,       ///< UTF-8 String Pair (User Property only)
    BinaryData            ///< Binary Data
>;

/**
 * @brief A single MQTT 5.0 property: identifier paired with its typed value.
 */
struct Property {
    PropertyId    id;     ///< Property identifier.
    PropertyValue value;  ///< Property value; type is determined by @p id.

    bool operator==(const Property&) const noexcept = default;
};

} // namespace mqtt
