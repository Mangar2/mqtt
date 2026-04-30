#include <catch2/catch_test_macros.hpp>

#include "yaha/ini/ini_document.h"
#include "yaha/ini/ini_value_reader.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path makeTempDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_ini_value_reader_test_" + std::to_string(stamp));
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

TEST_CASE("ini_value_reader_parses_bounded_unsigned", "[ini]") {
    std::uint64_t parsed = 0U;
    REQUIRE(yaha::iniTryParseUnsigned("42", 0U, 100U, parsed));
    REQUIRE(parsed == 42U);
    REQUIRE_FALSE(yaha::iniTryParseUnsigned("-1", 0U, 100U, parsed));
    REQUIRE_FALSE(yaha::iniTryParseUnsigned("101", 0U, 100U, parsed));
}

TEST_CASE("ini_value_reader_reads_optional_unsigned_field", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[mqtt]\n"
        "port = 1884\n");

    yaha::IniDocument document{};
    std::string errorMessage{};
    REQUIRE(yaha::IniDocument::tryLoadFromFile(iniPath, document, errorMessage));

    std::uint64_t parsed = 0U;
    REQUIRE(yaha::iniTryReadUnsigned(document,
                                     "mqtt",
                                     "port",
                                     1U,
                                     65535U,
                                     parsed,
                                     "mqtt.port",
                                     errorMessage));
    REQUIRE(parsed == 1884U);

    std::uint64_t missingOutput = 777U;
    REQUIRE(yaha::iniTryReadUnsigned(document,
                                     "mqtt",
                                     "missing",
                                     1U,
                                     65535U,
                                     missingOutput,
                                     "mqtt.missing",
                                     errorMessage));
    REQUIRE(missingOutput == 777U);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("ini_value_reader_reports_invalid_unsigned_field", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[mqtt]\n"
        "port = invalid\n");

    yaha::IniDocument document{};
    std::string errorMessage{};
    REQUIRE(yaha::IniDocument::tryLoadFromFile(iniPath, document, errorMessage));

    std::uint64_t parsed = 0U;
    REQUIRE_FALSE(yaha::iniTryReadUnsigned(document,
                                           "mqtt",
                                           "port",
                                           1U,
                                           65535U,
                                           parsed,
                                           "mqtt.port",
                                           errorMessage));
    REQUIRE(errorMessage == "invalid mqtt.port");

    removeDirectoryQuiet(tempDir);
}
