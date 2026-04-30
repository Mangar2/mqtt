#pragma once

/**
 * @file ini_document.h
 * @brief Generic INI document parser with multi-value support.
 */

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace yaha {

/**
 * @brief Parsed INI document with section/key/value access.
 */
class IniDocument {
public:
    /**
     * @brief One key/value line from an INI section.
     */
    struct Entry {
        std::string key{};      ///< Parsed key.
        std::string value{};    ///< Parsed value.
    };

    /**
     * @brief One INI section preserving insertion order and multi-values.
     */
    class Section {
    public:
        /**
         * @brief Appends one parsed key/value pair.
         * @param key Parsed key.
         * @param value Parsed value.
         */
        void addEntry(std::string key, std::string value);

        /**
         * @brief Returns all parsed entries in insertion order.
         * @return Ordered entry list.
         */
        [[nodiscard]] const std::vector<Entry>& entries() const;

        /**
         * @brief Returns all values for one key.
         * @param key Key name.
         * @return Value list when key exists, otherwise nullopt.
         */
        [[nodiscard]] std::optional<std::vector<std::string>> valuesForKey(std::string_view key) const;

        /**
         * @brief Returns the last value for one key.
         * @param key Key name.
         * @return Last value when key exists, otherwise nullopt.
         */
        [[nodiscard]] std::optional<std::string> lastValueForKey(std::string_view key) const;

    private:
        std::vector<Entry> entries_{};
        std::unordered_map<std::string, std::vector<std::string>> valuesByKey_{};
    };

    /**
     * @brief Loads and parses INI file into output document.
     * @param filePath Source INI file path.
     * @param output Parsed output document.
     * @param errorMessage Human-readable parser error text.
     * @return True when parsing succeeded.
     */
    [[nodiscard]] static bool tryLoadFromFile(
        const std::filesystem::path& filePath,
        IniDocument& output,
        std::string& errorMessage);

    /**
     * @brief Finds one section by name.
     * @param sectionName Section name.
     * @return Section pointer when found, otherwise nullptr.
     */
    [[nodiscard]] const Section* findSection(std::string_view sectionName) const;

    /**
     * @brief Returns last value for section/key pair.
     * @param sectionName Section name.
     * @param key Key name.
     * @return Last value when section and key exist, otherwise nullopt.
     */
    [[nodiscard]] std::optional<std::string> lastValue(
        std::string_view sectionName,
        std::string_view key) const;

private:
    std::unordered_map<std::string, Section> sections_{};
};

} // namespace yaha
