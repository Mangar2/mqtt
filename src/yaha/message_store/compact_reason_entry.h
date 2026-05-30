#pragma once

/**
 * @file compact_reason_entry.h
 * @brief CompactReasonEntry for MessageTree internal compressed reason storage.
 */

#include <cstdint>
#include <string>

namespace yaha {

/**
 * @brief Compact internal representation of one reason-chain entry.
 *
 * Stores the reason message text together with a parsed millisecond timestamp
 * and the original fractional-digit count so that ISO-8601 round-trips are
 * lossless with respect to precision.
 */
class CompactReasonEntry {
public:
    CompactReasonEntry() = default;

    /**
     * @brief Creates one entry from a raw message text and an ISO-8601 timestamp string.
     * @param message Reason message text.
     * @param timestampText ISO-8601 timestamp string; empty is accepted.
     * @return Constructed entry with parsed timestamp fields.
     */
    [[nodiscard]] static CompactReasonEntry fromStrings(std::string message,
                                                        const std::string& timestampText);

    [[nodiscard]] const std::string& message() const;
    [[nodiscard]] std::int64_t timestampMs() const;
    [[nodiscard]] std::uint8_t fractionalDigits() const;

    /**
     * @brief Formats the stored timestamp back to an ISO-8601 string with original precision.
     * @return ISO-8601 timestamp string; empty when no timestamp is stored.
     */
    [[nodiscard]] std::string toTimestampString() const;

private:
    CompactReasonEntry(std::string message,
                       std::int64_t timestampMs,
                       std::uint8_t fractionalDigits);

    std::string message_;
    std::int64_t timestampMs_{0};
    std::uint8_t fractionalDigits_{0};
};

} // namespace yaha
