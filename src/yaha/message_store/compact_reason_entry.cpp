#include "yaha/message_store/compact_reason_entry.h"

#include "yaha/message_store/iso_timestamp_parser.h"

#include <string>

namespace yaha {

namespace {

[[nodiscard]] std::int64_t parseTimestampMilliseconds(const std::string& timestampText) {
    std::int64_t timestampValue = 0;
    if (!tryParseIsoTimestampMilliseconds(timestampText, timestampValue)) {
        return 0;
    }
    return timestampValue;
}

[[nodiscard]] std::uint8_t parseFractionalDigits(const std::string& timestampText) {
    const std::size_t dotPos = timestampText.find('.');
    if (dotPos == std::string::npos) {
        return 0;
    }

    std::size_t zonePos = timestampText.find('Z', dotPos + 1U);
    if (zonePos == std::string::npos) {
        zonePos = timestampText.find('+', dotPos + 1U);
    }
    if (zonePos == std::string::npos) {
        zonePos = timestampText.find('-', dotPos + 1U);
    }
    if (zonePos == std::string::npos || zonePos <= dotPos + 1U) {
        return 0;
    }

    return static_cast<std::uint8_t>(zonePos - dotPos - 1U);
}

[[nodiscard]] std::string formatTimestampWithMetadata(std::int64_t timestampMs,
                                                      std::uint8_t fractionalDigits) {
    if (timestampMs == 0) {
        return std::string{};
    }

    std::string utcText = toIsoTimestampMilliseconds(timestampMs);

    if (utcText.empty() || utcText.back() != 'Z') {
        return utcText;
    }

    if (fractionalDigits > 0U && utcText.find('.') == std::string::npos) {
        const std::size_t zPos = utcText.size() - 1U;
        utcText.insert(zPos, ".000");
    }

    if (fractionalDigits == 0) {
        const std::size_t dotPos = utcText.find('.');
        if (dotPos != std::string::npos) {
            const std::size_t zPos = utcText.find('Z', dotPos + 1U);
            if (zPos != std::string::npos) {
                utcText.erase(dotPos, zPos - dotPos);
            }
        }
    } else if (fractionalDigits < 3U) {
        const std::size_t dotPos = utcText.find('.');
        const std::size_t zPos = utcText.find('Z', dotPos + 1U);
        if (dotPos != std::string::npos && zPos != std::string::npos) {
            utcText.erase(dotPos + 1U + fractionalDigits,
                          zPos - (dotPos + 1U + fractionalDigits));
        }
    }

    return utcText;
}

} // namespace

CompactReasonEntry::CompactReasonEntry(std::string message,
                                       std::int64_t timestampMs,
                                       std::uint8_t fractionalDigits)
    : message_(std::move(message))
    , timestampMs_(timestampMs)
    , fractionalDigits_(fractionalDigits) {}

CompactReasonEntry CompactReasonEntry::fromStrings(std::string message,
                                                   const std::string& timestampText) {
    return CompactReasonEntry{
        std::move(message),
        parseTimestampMilliseconds(timestampText),
        parseFractionalDigits(timestampText)
    };
}

const std::string& CompactReasonEntry::message() const {
    return message_;
}

std::int64_t CompactReasonEntry::timestampMs() const {
    return timestampMs_;
}

std::uint8_t CompactReasonEntry::fractionalDigits() const {
    return fractionalDigits_;
}

std::string CompactReasonEntry::toTimestampString() const {
    return formatTimestampWithMetadata(timestampMs_, fractionalDigits_);
}

} // namespace yaha
