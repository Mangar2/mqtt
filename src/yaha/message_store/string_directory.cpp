#include "yaha/message_store/string_directory.h"

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace yaha {

namespace {

constexpr std::size_t k_max_entry_count{
    static_cast<std::size_t>(std::numeric_limits<slotIndex_t>::max()) + 1U
};

} // namespace

slotIndex_t StringDirectory::add(const std::string& text) {
    if (text.empty()) {
        throw std::invalid_argument{"StringDirectory::add does not accept empty strings"};
    }

    std::optional<std::size_t> firstFreeIndex{};

    for (std::size_t entryIndex = 0U; entryIndex < entries_.size(); ++entryIndex) {
        const auto& currentText = entries_[entryIndex];
        if (currentText == text) {
            return static_cast<slotIndex_t>(entryIndex);
        }

        if (currentText.empty() && !firstFreeIndex.has_value()) {
            firstFreeIndex = entryIndex;
        }
    }

    if (firstFreeIndex.has_value()) {
        entries_[firstFreeIndex.value()] = text;
        nonEmptyCount_ += 1U;
        return static_cast<slotIndex_t>(firstFreeIndex.value());
    }

    if (entries_.size() >= k_max_entry_count) {
        throw std::overflow_error{"StringDirectory index space exhausted"};
    }

    entries_.push_back(text);
    nonEmptyCount_ += 1U;
    return static_cast<slotIndex_t>(entries_.size() - 1U);
}

std::optional<std::string> StringDirectory::get(slotIndex_t slotIndex) const {
    const auto entryIndex = static_cast<std::size_t>(slotIndex);
    if (entryIndex >= entries_.size()) {
        return std::nullopt;
    }

    const auto& storedText = entries_[entryIndex];
    if (storedText.empty()) {
        return std::nullopt;
    }

    return storedText;
}

bool StringDirectory::remove(slotIndex_t slotIndex) {
    const auto entryIndex = static_cast<std::size_t>(slotIndex);
    if (entryIndex >= entries_.size()) {
        return false;
    }

    auto& storedText = entries_[entryIndex];
    if (storedText.empty()) {
        return false;
    }

    storedText.clear();
    nonEmptyCount_ -= 1U;
    return true;
}

slotIndex_t StringDirectory::size() const {
    return nonEmptyCount_;
}

slotIndex_t StringDirectory::capacity() const {
    return static_cast<slotIndex_t>(entries_.size());
}

} // namespace yaha