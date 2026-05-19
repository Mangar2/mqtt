#include "yaha/message/message_log_filter.h"

#include <string>
#include <vector>

namespace yaha {

namespace {

[[nodiscard]] std::vector<std::string> splitTopicSegments(const std::string_view topicText) {
    std::vector<std::string> segments{};
    std::size_t segmentStart = 0U;
    while (segmentStart <= topicText.size()) {
        const std::size_t segmentEnd = topicText.find('/', segmentStart);
        if (segmentEnd == std::string_view::npos) {
            segments.emplace_back(topicText.substr(segmentStart));
            break;
        }

        segments.emplace_back(topicText.substr(segmentStart, segmentEnd - segmentStart));
        segmentStart = segmentEnd + 1U;
    }

    return segments;
}

} // namespace

bool matchesTopicFilter(const std::string_view topicName, const std::string_view topicFilter) {
    if (topicFilter.empty()) {
        return topicName.empty();
    }

    const std::vector<std::string> topicSegments = splitTopicSegments(topicName);
    const std::vector<std::string> filterSegments = splitTopicSegments(topicFilter);

    std::size_t topicIndex = 0U;
    for (std::size_t filterIndex = 0U; filterIndex < filterSegments.size(); ++filterIndex) {
        const std::string& filterSegment = filterSegments[filterIndex];

        if (filterSegment == "#") {
            return filterIndex + 1U == filterSegments.size();
        }

        if (topicIndex >= topicSegments.size()) {
            return false;
        }

        if (filterSegment != "+" && filterSegment != topicSegments[topicIndex]) {
            return false;
        }

        ++topicIndex;
    }

    return topicIndex == topicSegments.size();
}

} // namespace yaha
