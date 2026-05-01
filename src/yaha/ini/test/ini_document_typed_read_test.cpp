#include <catch2/catch_test_macros.hpp>

#include "yaha/ini/ini_document.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path makeTempDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_ini_document_typed_read_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void removeDirectoryQuiet(const std::filesystem::path& path) {
    std::error_code error{};
    std::filesystem::remove_all(path, error);
}

std::filesystem::path writeIniFile(
    const std::filesystem::path& directory,
    const std::string& content) {
    const auto path = directory / "config.ini";
    std::ofstream output{path};
    output << content;
    return path;
}

} // namespace

TEST_CASE("ini_document_parses_bounded_unsigned", "[ini]") {
    const auto parsed = yaha::IniDocument::parseUnsigned("42", 0U, 100U);
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == 42U);
    REQUIRE_FALSE(yaha::IniDocument::parseUnsigned("-1", 0U, 100U).has_value());
    REQUIRE_FALSE(yaha::IniDocument::parseUnsigned("101", 0U, 100U).has_value());
}

TEST_CASE("ini_document_reads_optional_unsigned_field", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[mqtt]\n"
        "port = 1884\n");

    const auto document = yaha::IniDocument::loadFromFile(iniPath);

    const auto parsed = document.readUnsigned("mqtt", "port", 1U, 65535U);
    REQUIRE(parsed.second.empty());
    REQUIRE(parsed.first.has_value());
    REQUIRE(*parsed.first == 1884U);

    const auto missingValue = document.readUnsigned("mqtt", "missing", 1U, 65535U);
    REQUIRE(missingValue.second.empty());
    REQUIRE_FALSE(missingValue.first.has_value());

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("ini_document_reports_invalid_unsigned_field", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[mqtt]\n"
        "port = invalid\n");

    const auto document = yaha::IniDocument::loadFromFile(iniPath);

    const auto parsed = document.readUnsigned("mqtt", "port", 1U, 65535U);
    REQUIRE_FALSE(parsed.first.has_value());
    REQUIRE(parsed.second == "invalid unsigned value for 'mqtt.port' (expected 1..65535, got 'invalid')");

    removeDirectoryQuiet(tempDir);
}
