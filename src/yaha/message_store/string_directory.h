#pragma once

/**
 * @file string_directory.h
 * @brief Small vector-backed string directory with reusable slots.
 */

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yaha {

using slotIndex_t = std::uint16_t;

/**
 * @brief Stores a small set of unique strings in reusable indexed slots.
 *
 * Empty slots are represented by empty strings and can be reused by `add`.
 * The directory uses a linear search and intentionally does not use maps.
 */
class StringDirectory {
public:
    /**
     * @brief Adds one string or returns the existing slot index for duplicates.
     * @param text String to insert.
     * @return Slot index in range `[0, 65535]`.
     */
    [[nodiscard]] slotIndex_t add(const std::string& text);

    /**
     * @brief Returns the string stored at one slot index.
     * @param slotIndex Slot index.
     * @return Stored string when slot is valid and non-empty; otherwise `std::nullopt`.
     */
    [[nodiscard]] std::optional<std::string> get(slotIndex_t slotIndex) const;

    /**
     * @brief Deletes one slot by setting it to an empty string.
     * @param slotIndex Slot index.
     * @return True when a non-empty slot was cleared; otherwise false.
     */
    [[nodiscard]] bool remove(slotIndex_t slotIndex);

    /**
     * @brief Returns the number of non-empty entries.
     * @return Number of stored strings excluding empty slots.
     */
    [[nodiscard]] slotIndex_t size() const;

    /**
     * @brief Returns the number of allocated slots including empty slots.
     * @return Capacity-like slot count.
     */
    [[nodiscard]] slotIndex_t capacity() const;

private:
    std::vector<std::string> entries_{};
    slotIndex_t nonEmptyCount_{0U};
};

} // namespace yaha