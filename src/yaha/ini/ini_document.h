#pragma once

/**
 * @file ini_document.h
 * @brief Generic INI document parser with multi-value support.
 */

#include <filesystem>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yaha {

/**
 * @brief Callback invoked when a config value falls back to its default.
 *
 * Parameters: service name (as given to `IniDocument::loadFromFile`), section name, key
 * name, raw value text as found in the INI file (or `<missing>`-style placeholder chosen by
 * the caller), default value text that was applied, and a human-readable reason.
 */
using ConfigWarningHandler = std::function<void(
    std::string_view serviceName,
    std::string_view sectionName,
    std::string_view keyName,
    const std::string& rawValue,
    const std::string& defaultValue,
    const std::string& reasonText)>;

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
     * @brief Loads and parses one INI file.
     * @param filePath Source INI file path.
     * @param serviceName Service name forwarded to config-fallback warning handlers.
     * @return Parsed INI document with one default warning handler already registered.
     * @throws std::runtime_error when file open/read or parse fails.
     */
    [[nodiscard]] static IniDocument loadFromFile(
        const std::filesystem::path& filePath,
        std::string serviceName = {});

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

    /**
     * @brief Parses one unsigned integer with inclusive bounds.
     * @param text Input text.
     * @param minValue Lower inclusive bound.
     * @param maxValue Upper inclusive bound.
     * @return Parsed result when parsing and bounds validation succeeded.
     */
    [[nodiscard]] static std::optional<std::uint64_t> parseUnsigned(
        std::string_view text,
        std::uint64_t minValue,
        std::uint64_t maxValue);

    /**
     * @brief Reads one optional unsigned value and returns value/error as function result.
     * @param sectionName Section name.
     * @param key Key name.
     * @param minValue Lower inclusive bound.
     * @param maxValue Upper inclusive bound.
     * @return Pair of parsed optional value and error text.
     */
    [[nodiscard]] std::pair<std::optional<std::uint64_t>, std::string> readUnsigned(
        std::string_view sectionName,
        std::string_view key,
        std::uint64_t minValue,
        std::uint64_t maxValue) const;

    /**
     * @brief Reads one optional boolean value and returns value/error as function result.
     *
     * Accepted true values: true, 1, yes, on
     * Accepted false values: false, 0, no, off
     *
     * @param sectionName Section name.
     * @param key Key name.
     * @return Pair of parsed optional value and error text.
     */
    [[nodiscard]] std::pair<std::optional<bool>, std::string> readBool(
        std::string_view sectionName,
        std::string_view key) const;

    /**
     * @brief Registers one additional config-fallback warning handler.
     * @param handler Handler invoked by future `reportFallback` calls.
     */
    void addWarningHandler(ConfigWarningHandler handler) const;

    /**
     * @brief Removes all registered config-fallback warning handlers.
     *
     * This also removes the default handler installed by `loadFromFile`.
     */
    void clearWarningHandlers() const;

    /**
     * @brief Invokes all registered config-fallback warning handlers.
     * @param sectionName Section name of the affected key.
     * @param keyName Key name of the affected value.
     * @param rawValue Raw value text that triggered the fallback.
     * @param defaultValue Default value text that was applied instead.
     * @param reasonText Human-readable reason for the fallback.
     */
    void reportFallback(
        std::string_view sectionName,
        std::string_view keyName,
        const std::string& rawValue,
        const std::string& defaultValue,
        const std::string& reasonText) const;

private:
    std::unordered_map<std::string, Section> sections_{};
    std::string serviceName_{};
    mutable std::vector<ConfigWarningHandler> warningHandlers_{};
};

} // namespace yaha
