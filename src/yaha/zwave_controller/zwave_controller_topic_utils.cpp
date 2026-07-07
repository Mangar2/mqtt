#include "yaha/zwave_controller/zwave_controller_topic_utils.h"

#include "yaha/zwave_controller/zwave_controller_value_utils.h"

#include <cmath>
#include <limits>

namespace yaha::zwave_controller_topic_utils {
namespace {

constexpr double kIntegerTolerance = 1e-9;

} // namespace

std::optional<std::uint16_t> parseNodeIdFromValue(const Value& value) {
    const double numericValue = zwave_controller_value_utils::valueAsDouble(value);
    if (numericValue < 0.0 || numericValue > static_cast<double>(std::numeric_limits<std::uint16_t>::max())) {
        return std::nullopt;
    }

    const double rounded = std::round(numericValue);
    if (std::fabs(rounded - numericValue) > kIntegerTolerance) {
        return std::nullopt;
    }

    return static_cast<std::uint16_t>(rounded);
}

std::optional<std::string> parseOptionalLabelFromSetTopic(const std::vector<std::string>& topicParts) {
    if (topicParts.size() <= kSetTopicMinimumParts) {
        return std::nullopt;
    }

    const std::string& label = topicParts[topicParts.size() - kSetTopicMinimumParts];
    if (label.empty()) {
        return std::nullopt;
    }
    return label;
}

std::string joinTopicParts(const std::vector<std::string>& parts, const std::size_t count) {
    if (count == 0U) {
        return std::string{};
    }

    std::string joined = parts.front();
    for (std::size_t index = 1U; index < count; ++index) {
        joined += "/" + parts[index];
    }
    return joined;
}

std::vector<std::string> splitTopic(const std::string& topic) {
    std::vector<std::string> parts{};
    std::size_t segmentStart = 0U;
    while (segmentStart <= topic.size()) {
        const std::size_t segmentEnd = topic.find('/', segmentStart);
        if (segmentEnd == std::string::npos) {
            parts.push_back(topic.substr(segmentStart));
            break;
        }

        parts.push_back(topic.substr(segmentStart, segmentEnd - segmentStart));
        segmentStart = segmentEnd + 1U;
    }
    return parts;
}

} // namespace yaha::zwave_controller_topic_utils
