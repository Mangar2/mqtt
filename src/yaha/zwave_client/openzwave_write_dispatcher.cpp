#include "yaha/zwave_client/openzwave_write_dispatcher.h"

#include "Manager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

namespace yaha {
namespace {

constexpr std::uint16_t kRuntimeSwitchMultilevelClass = 0x26U;
constexpr std::uint16_t kConfigParamValueSize = 2U;
constexpr float kNumericTolerance = 1e-6F;

[[nodiscard]] std::uint8_t requireUint8(const std::uint16_t value, const std::string& fieldName) {
    if (value > static_cast<std::uint16_t>(std::numeric_limits<std::uint8_t>::max())) {
        throw std::runtime_error(fieldName + " out of range");
    }
    return static_cast<std::uint8_t>(value);
}

[[nodiscard]] int32_t roundToInt32(const double value, const std::string& fieldName) {
    const double rounded = std::round(value);
    if (std::fabs(rounded - value) > kNumericTolerance) {
        throw std::runtime_error(fieldName + " requires integer value");
    }

    if (rounded < static_cast<double>(std::numeric_limits<int32_t>::min())
        || rounded > static_cast<double>(std::numeric_limits<int32_t>::max())) {
        throw std::runtime_error(fieldName + " out of range");
    }

    return static_cast<int32_t>(rounded);
}

[[nodiscard]] OpenZWave::ValueID::ValueGenre decodeGenreOrDefault(const std::uint8_t cachedGenreCode) {
    switch (cachedGenreCode) {
    case 0U:
        return OpenZWave::ValueID::ValueGenre_Basic;
    case 1U:
        return OpenZWave::ValueID::ValueGenre_User;
    case 2U:
        return OpenZWave::ValueID::ValueGenre_Config;
    case 3U:
        return OpenZWave::ValueID::ValueGenre_System;
    default:
        return OpenZWave::ValueID::ValueGenre_User;
    }
}

[[nodiscard]] std::string toLower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] OpenZWave::ValueID::ValueType toValueType(const std::string& typeName) {
    if (typeName == "bool" || typeName == "switch") {
        return OpenZWave::ValueID::ValueType_Bool;
    }

    if (typeName == "byte") {
        return OpenZWave::ValueID::ValueType_Byte;
    }

    if (typeName == "short") {
        return OpenZWave::ValueID::ValueType_Short;
    }

    if (typeName == "int" || typeName == "integer") {
        return OpenZWave::ValueID::ValueType_Int;
    }

    if (typeName == "decimal" || typeName == "float" || typeName == "number") {
        return OpenZWave::ValueID::ValueType_Decimal;
    }

    if (typeName == "list") {
        return OpenZWave::ValueID::ValueType_List;
    }

    if (typeName == "button") {
        return OpenZWave::ValueID::ValueType_Button;
    }

    return OpenZWave::ValueID::ValueType_String;
}

[[nodiscard]] std::string valueTypeNameForLog(const OpenZWave::ValueID::ValueType valueType) {
    switch (valueType) {
    case OpenZWave::ValueID::ValueType_Bool:
        return "bool";
    case OpenZWave::ValueID::ValueType_Byte:
        return "byte";
    case OpenZWave::ValueID::ValueType_Decimal:
        return "decimal";
    case OpenZWave::ValueID::ValueType_Int:
        return "int";
    case OpenZWave::ValueID::ValueType_List:
        return "list";
    case OpenZWave::ValueID::ValueType_Schedule:
        return "schedule";
    case OpenZWave::ValueID::ValueType_Short:
        return "short";
    case OpenZWave::ValueID::ValueType_String:
        return "string";
    case OpenZWave::ValueID::ValueType_Button:
        return "button";
    case OpenZWave::ValueID::ValueType_Raw:
        return "raw";
    case OpenZWave::ValueID::ValueType_BitSet:
        return "bitset";
    default:
        return "unknown";
    }
}

[[nodiscard]] std::string valuePayloadForLog(const std::variant<bool, double, std::string>& value) {
    if (const auto* booleanValue = std::get_if<bool>(&value); booleanValue != nullptr) {
        return *booleanValue ? "true" : "false";
    }

    if (const auto* numericValue = std::get_if<double>(&value); numericValue != nullptr) {
        std::ostringstream stream{};
        stream << *numericValue;
        return stream.str();
    }

    return std::get<std::string>(value);
}

[[nodiscard]] OpenZWave::ValueID buildWriteTargetValueId(
    const std::uint32_t homeId,
    const ZwaveResolvedId& target,
    const OpenZWave::ValueID::ValueGenre valueGenre,
    const OpenZWave::ValueID::ValueType valueType) {
    return OpenZWave::ValueID{
        homeId,
        requireUint8(target.nodeId, "node id"),
        valueGenre,
        requireUint8(target.classId, "class id"),
        requireUint8(target.instance, "instance"),
        target.index,
        valueType};
}

[[nodiscard]] bool writeBooleanValue(
    OpenZWave::Manager& manager,
    const OpenZWave::ValueID& valueId,
    const OpenZWave::ValueID::ValueType valueType,
    const bool booleanValue) {
    if (valueType == OpenZWave::ValueID::ValueType_Bool) {
        return manager.SetValue(valueId, booleanValue);
    }

    if (valueType == OpenZWave::ValueID::ValueType_Byte) {
        return manager.SetValue(valueId, static_cast<std::uint8_t>(booleanValue ? 1U : 0U));
    }

    if (valueType == OpenZWave::ValueID::ValueType_Short) {
        return manager.SetValue(valueId, static_cast<int16_t>(booleanValue ? 1 : 0));
    }

    if (valueType == OpenZWave::ValueID::ValueType_Int) {
        return manager.SetValue(valueId, static_cast<int32_t>(booleanValue ? 1 : 0));
    }

    if (valueType == OpenZWave::ValueID::ValueType_Decimal) {
        return manager.SetValue(valueId, booleanValue ? 1.0F : 0.0F);
    }

    if (valueType == OpenZWave::ValueID::ValueType_List) {
        return manager.SetValueListSelection(valueId, booleanValue ? "on" : "off");
    }

    return manager.SetValue(valueId, std::string{booleanValue ? "on" : "off"});
}

[[nodiscard]] bool writeNumericValue(
    OpenZWave::Manager& manager,
    const OpenZWave::ValueID& valueId,
    const OpenZWave::ValueID::ValueType valueType,
    const double numericValue) {
    if (valueType == OpenZWave::ValueID::ValueType_Bool) {
        return manager.SetValue(valueId, std::fabs(numericValue - 0.0) > kNumericTolerance);
    }

    if (valueType == OpenZWave::ValueID::ValueType_Byte) {
        const int32_t rounded = roundToInt32(numericValue, "byte value");
        if (rounded < 0 || rounded > static_cast<int32_t>(std::numeric_limits<std::uint8_t>::max())) {
            throw std::runtime_error("byte value out of range");
        }
        return manager.SetValue(valueId, static_cast<std::uint8_t>(rounded));
    }

    if (valueType == OpenZWave::ValueID::ValueType_Short) {
        const int32_t rounded = roundToInt32(numericValue, "short value");
        if (rounded < static_cast<int32_t>(std::numeric_limits<int16_t>::min())
            || rounded > static_cast<int32_t>(std::numeric_limits<int16_t>::max())) {
            throw std::runtime_error("short value out of range");
        }
        return manager.SetValue(valueId, static_cast<int16_t>(rounded));
    }

    if (valueType == OpenZWave::ValueID::ValueType_Int) {
        return manager.SetValue(valueId, roundToInt32(numericValue, "int value"));
    }

    if (valueType == OpenZWave::ValueID::ValueType_Decimal) {
        return manager.SetValue(valueId, static_cast<float>(numericValue));
    }

    return manager.SetValue(valueId, std::to_string(numericValue));
}

[[nodiscard]] bool writeTextValue(
    OpenZWave::Manager& manager,
    const OpenZWave::ValueID& valueId,
    const OpenZWave::ValueID::ValueType valueType,
    const std::string& textValue) {
    if (valueType == OpenZWave::ValueID::ValueType_List) {
        return manager.SetValueListSelection(valueId, textValue);
    }

    return manager.SetValue(valueId, textValue);
}

} // namespace

OpenZwaveWriteDispatcher::WriteMetadata OpenZwaveWriteDispatcher::buildWriteMetadata(
    const ZwaveResolvedId& target,
    const std::uint8_t genreCode,
    const std::variant<bool, double, std::string>& value) {
    const std::string normalizedType = toLower(target.type);
    auto valueType = toValueType(normalizedType);
    if (valueType == OpenZWave::ValueID::ValueType_Bool && target.classId == kRuntimeSwitchMultilevelClass) {
        valueType = OpenZWave::ValueID::ValueType_Byte;
    }

    return WriteMetadata{
        .normalizedType = normalizedType,
        .valueTypeCode = static_cast<std::uint8_t>(valueType),
        .genreCode = genreCode,
        .valueTypeName = valueTypeNameForLog(valueType),
        .payloadText = valuePayloadForLog(value)};
}

bool OpenZwaveWriteDispatcher::write(
    OpenZWave::Manager& manager,
    const std::uint32_t homeId,
    const ZwaveResolvedId& target,
    const WriteMetadata& metadata,
    const std::variant<bool, double, std::string>& value) {
    const auto valueType = static_cast<OpenZWave::ValueID::ValueType>(metadata.valueTypeCode);
    const OpenZWave::ValueID valueId = buildWriteTargetValueId(
        homeId,
        target,
        decodeGenreOrDefault(metadata.genreCode),
        valueType);

    if (const auto* booleanValue = std::get_if<bool>(&value); booleanValue != nullptr) {
        return writeBooleanValue(manager, valueId, valueType, *booleanValue);
    }

    if (const auto* numericValue = std::get_if<double>(&value); numericValue != nullptr) {
        return writeNumericValue(manager, valueId, valueType, *numericValue);
    }

    return writeTextValue(manager, valueId, valueType, std::get<std::string>(value));
}

void OpenZwaveWriteDispatcher::setConfigParam(
    OpenZWave::Manager& manager,
    const std::uint32_t homeId,
    const std::uint16_t nodeId,
    const std::uint16_t paramId,
    const double value) {
    (void)manager.SetConfigParam(
        homeId,
        requireUint8(nodeId, "node id"),
        requireUint8(paramId, "param id"),
        roundToInt32(value, "config param value"),
        static_cast<std::uint8_t>(kConfigParamValueSize));
}

} // namespace yaha
