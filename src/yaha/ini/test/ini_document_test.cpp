#include <catch2/catch_test_macros.hpp>

#include "yaha/ini/ini_document.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path makeTempDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_ini_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void removeDirectoryQuiet(const std::filesystem::path& path) {
    std::error_code error{};
    std::filesystem::remove_all(path, error);
}

std::filesystem::path writeIniFile(const std::filesystem::path& directory,
                                   const std::string& content) {
    const auto path = directory / "config.ini";
    std::ofstream output{path};
    output << content;
    return path;
}

} // namespace

TEST_CASE("load_ini_with_sections_and_values", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[mqtt]\n"
        "host = broker.local\n"
        "port = 1883\n"
        "\n"
        "[server]\n"
        "path = /store\n");

    const auto document = yaha::IniDocument::loadFromFile(iniPath);
    REQUIRE(document.lastValue("mqtt", "host").value_or("") == "broker.local");
    REQUIRE(document.lastValue("mqtt", "port").value_or("") == "1883");
    REQUIRE(document.lastValue("server", "path").value_or("") == "/store");

    removeDirectoryQuiet(tempDir);
}

void validateMultiValueSection(
    const yaha::IniDocument::Section* section,
    const std::string& key,
    const std::vector<std::string>& expectedValues) {
    REQUIRE(section != nullptr);
    const auto values = section->valuesForKey(key);
    REQUIRE(values.has_value());
    REQUIRE(values->size() == expectedValues.size());
    for (size_t i = 0; i < expectedValues.size(); ++i) {
        REQUIRE((*values)[i] == expectedValues[i]);
    }
    REQUIRE(section->lastValueForKey(key).value_or("") == expectedValues.back());
}

TEST_CASE("load_ini_preserves_multivalue_keys", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[tracing]\n"
        "module = broker\n"
        "module = connection\n"
        "module = monitoring\n");

    const auto document = yaha::IniDocument::loadFromFile(iniPath);

    validateMultiValueSection(document.findSection("tracing"), "module",
        {"broker", "connection", "monitoring"});

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_ini_rejects_missing_equals", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[mqtt]\n"
        "host broker.local\n");

    try {
        const auto document = yaha::IniDocument::loadFromFile(iniPath);
        (void)document;
        FAIL("expected parse exception");
    } catch (const std::exception& exceptionValue) {
        REQUIRE(std::string{exceptionValue.what()}.find("missing '='") != std::string::npos);
    }

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_ini_rejects_empty_section_name", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[]\n"
        "host = broker.local\n");

    try {
        const auto document = yaha::IniDocument::loadFromFile(iniPath);
        (void)document;
        FAIL("expected parse exception");
    } catch (const std::exception& exceptionValue) {
        REQUIRE(std::string{exceptionValue.what()}.find("empty section name") != std::string::npos);
    }

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_ini_supports_hash_and_inline_comments", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "# top-level comment\n"
        "[mqtt] ; section comment\n"
        "host = broker.local\n"
        "topicFilter = #\n"
        "password = abc#123\n"
        "topic = \"a#b\" ; quoted hash must not start comment\n");

    const auto document = yaha::IniDocument::loadFromFile(iniPath);
    REQUIRE(document.lastValue("mqtt", "host").value_or("") == "broker.local");
    REQUIRE(document.lastValue("mqtt", "topicFilter").value_or("") == "#");
    REQUIRE(document.lastValue("mqtt", "password").value_or("") == "abc#123");
    REQUIRE(document.lastValue("mqtt", "topic").value_or("") == "\"a#b\"");

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_ini_treats_hash_at_column_zero_as_comment_even_with_equals", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "# comment with equals = still comment\n"
        "[mqtt]\n"
        "host = broker.local\n");

    const auto document = yaha::IniDocument::loadFromFile(iniPath);
    REQUIRE(document.lastValue("mqtt", "host").value_or("") == "broker.local");

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_ini_rejects_indented_hash_line", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[mqtt]\n"
        "  # indented hash is not a comment\n"
        "host = broker.local\n");

    try {
        const auto document = yaha::IniDocument::loadFromFile(iniPath);
        (void)document;
        FAIL("expected parse exception");
    } catch (const std::exception& exceptionValue) {
        REQUIRE(std::string{exceptionValue.what()}.find("missing '='") != std::string::npos);
    }

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_ini_keeps_empty_section", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[subscriptions]\n");

    const auto document = yaha::IniDocument::loadFromFile(iniPath);
    const auto* section = document.findSection("subscriptions");
    REQUIRE(section != nullptr);
    REQUIRE(section->entries().empty());

    removeDirectoryQuiet(tempDir);
}
