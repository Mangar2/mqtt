#include "yaha/zwave_devices/zwave_devices_mapper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace yaha {

namespace {

constexpr std::uint16_t kZwaveConfigurationClass = 0x70U;
constexpr std::uint16_t kZwaveSwitchBinaryClass = 0x25U;
constexpr std::uint16_t kZwaveSwitchMultilevelClass = 0x26U;
constexpr double kUnitMatchTolerance = 1e-9;

[[nodiscard]] std::string defaultTypeForClassId(const std::optional<std::uint16_t>& classId) {
    if (!classId.has_value()) {
        return "bool";
    }

    if (*classId == kZwaveSwitchBinaryClass) {
        return "switch";
    }

    if (*classId == kZwaveSwitchMultilevelClass) {
        return "byte";
    }

    return "bool";
}

[[nodiscard]] std::string toStringValue(const Value& input) {
    if (const auto* text = std::get_if<std::string>(&input); text != nullptr) {
        return *text;
    }
    return std::to_string(std::get<double>(input));
}

[[nodiscard]] bool convertToBool(const Value& input) {
    if (const auto* text = std::get_if<std::string>(&input); text != nullptr) {
        return *text == "on" || *text == "1" || *text == "true";
    }

    return std::fabs(std::get<double>(input) - 1.0) < kUnitMatchTolerance;
}

[[nodiscard]] double convertToNumber(const Value& input) {
    if (const auto* number = std::get_if<double>(&input); number != nullptr) {
        return *number;
    }

    const auto& text = std::get<std::string>(input);
    std::size_t consumed = 0U;
    const double parsed = std::stod(text, &consumed);
    if (consumed != text.size()) {
        throw std::runtime_error("invalid numeric value '" + text + "'");
    }

    return parsed;
}

[[nodiscard]] std::uint8_t toUint8OrDefault(
    const std::optional<std::uint8_t>& value,
    const std::uint8_t fallback) {
    if (value.has_value()) {
        return *value;
    }
    return fallback;
}

[[nodiscard]] bool isCandidateMatch(
    const ZwaveDeviceConfig& device,
    const ZwaveValueDescriptor& descriptor) {
    const bool classMatches = !device.classId.has_value() || *device.classId == descriptor.classId;
    const bool instanceMatches = !device.instance.has_value() || *device.instance == descriptor.instance;
    const bool indexMatches = !device.index.has_value() || *device.index == descriptor.index;
    return classMatches && instanceMatches && indexMatches;
}

[[nodiscard]] std::uint8_t calculateMatchScore(
    const ZwaveDeviceConfig& device,
    const ZwaveValueDescriptor& descriptor) {
    std::uint8_t score = 1U;
    if (device.classId.has_value() && *device.classId == descriptor.classId) {
        score = static_cast<std::uint8_t>(score + 1U);
    }
    if (device.index.has_value() && *device.index == descriptor.index) {
        score = static_cast<std::uint8_t>(score + 2U);
    }
    if (device.instance.has_value() && *device.instance == descriptor.instance) {
        score = static_cast<std::uint8_t>(score + 4U);
    }
    return score;
}

[[nodiscard]] ZwaveTopicMapping buildTopicMapping(
    const ZwaveDeviceConfig& device,
    const ZwaveValueDescriptor& descriptor) {
    ZwaveTopicMapping mapped{};
    mapped.topic = device.topic;
    if (device.type.has_value() && !device.type->empty()) {
        mapped.type = *device.type;
    }

    if (!device.classId.has_value() && descriptor.label.has_value() && !descriptor.label->empty()) {
        mapped.topic += "/" + *descriptor.label;
    }

    return mapped;
}

} // namespace

ZwaveDevicesMapper::ZwaveDevicesMapper(std::vector<ZwaveDeviceConfig> devices)
    : devices_(std::move(devices)) {}

std::optional<ZwaveTopicMapping> ZwaveDevicesMapper::valueToTopicAndType(
    const ZwaveValueDescriptor& descriptor) {
    if (descriptor.valueId.has_value()) {
        const auto cacheIterator = valueIdToTopicCache_.find(*descriptor.valueId);
        if (cacheIterator != valueIdToTopicCache_.end()) {
            return cacheIterator->second;
        }
    }

    const auto mapping = findBestMatch(descriptor);
    if (mapping.has_value() && descriptor.valueId.has_value()) {
        valueIdToTopicCache_[*descriptor.valueId] = *mapping;
    }
    return mapping;
}

std::optional<ZwaveTopicMapping> ZwaveDevicesMapper::findBestMatch(
    const ZwaveValueDescriptor& descriptor) const {
    std::optional<ZwaveTopicMapping> bestMatch{};
    std::uint8_t bestScore = 0U;

    for (const auto& device : devices_) {
        if (device.nodeId != descriptor.nodeId) {
            continue;
        }

        if (!isCandidateMatch(device, descriptor)) {
            continue;
        }

        const std::uint8_t score = calculateMatchScore(device, descriptor);

        if (score <= bestScore) {
            continue;
        }

        bestMatch = buildTopicMapping(device, descriptor);
        bestScore = score;
    }

    return bestMatch;
}

ZwaveResolvedId ZwaveDevicesMapper::topicToZwaveId(
    const ZwaveNodeMap& nodes,
    const std::string& topic,
    const std::optional<std::string>& label) const {
    const std::string compoundTopic = makeTopicCacheKey(topic, label);

    const ZwaveDeviceConfig* selected = nullptr;
    for (const auto& device : devices_) {
        if (device.topic == compoundTopic) {
            selected = &device;
            break;
        }
    }

    if (selected == nullptr) {
        for (const auto& device : devices_) {
            if (device.topic == topic) {
                selected = &device;
                break;
            }
        }
    }

    if (selected == nullptr) {
        throw std::runtime_error("zwave set value with unknown topic " + topic);
    }

    ZwaveResolvedId resolved{};
    resolved.nodeId = selected->nodeId;
    resolved.instance = toUint8OrDefault(selected->instance, 1U);
    resolved.index = toUint8OrDefault(selected->index, 0U);
    resolved.type = selected->type.has_value() && !selected->type->empty()
        ? *selected->type
        : defaultTypeForClassId(selected->classId);

    if (!selected->classId.has_value()) {
        if (!label.has_value() || label->empty()) {
            throw std::runtime_error(
                "Neither label nor class_id specified, (configuration error) topic: " + topic);
        }

        const auto nodeIterator = nodes.find(resolved.nodeId);
        if (nodeIterator == nodes.end()) {
            throw std::runtime_error("node not found for topic " + topic);
        }

        const auto& objects = nodeIterator->second;
        const auto objectIterator = std::ranges::find_if(
            objects,
            [&](const ZwaveNodeObject& object) {
                return object.label == *label && object.instance == resolved.instance;
            });

        if (objectIterator == objects.end()) {
            throw std::runtime_error("zwave object label not found for topic " + topic);
        }

        resolved.classId = objectIterator->classId;
        resolved.index = objectIterator->index;
        resolved.type = objectIterator->type;
        return resolved;
    }

    resolved.classId = *selected->classId;
    return resolved;
}

ZwaveWriteRequest ZwaveDevicesMapper::buildWriteRequest(
    const ZwaveResolvedId& target,
    const Value& input) {
    ZwaveWriteRequest request{};
    request.target = target;

    if (target.classId == kZwaveConfigurationClass) {
        request.kind = ZwaveWriteKind::SetConfigParam;
        request.value = convertToNumber(input);
        return request;
    }

    request.kind = ZwaveWriteKind::SetValue;
    if (target.type == "bool" || target.type == "switch") {
        request.value = convertToBool(input);
    } else if (target.type == "byte") {
        request.value = convertToNumber(input);
    } else if (const auto* number = std::get_if<double>(&input); number != nullptr) {
        request.value = *number;
    } else {
        request.value = toStringValue(input);
    }

    return request;
}

std::string ZwaveDevicesMapper::makeTopicCacheKey(
    const std::string& topic,
    const std::optional<std::string>& label) {
    if (!label.has_value() || label->empty()) {
        return topic;
    }
    return topic + "/" + *label;
}

} // namespace yaha
