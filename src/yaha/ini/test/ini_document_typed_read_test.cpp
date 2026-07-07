#include <catch2/catch_test_macros.hpp>

#include "yaha/ini/ini_document.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct RecordedCall {
    std::string sectionName{};
    std::string keyName{};
    std::string rawValue{};
    std::string defaultValue{};
    std::string reasonText{};
};

yaha::ConfigWarningHandler makeProbe(std::vector<RecordedCall>& calls) {
    return [&calls](
               const std::string_view /*serviceName*/,
               const std::string_view sectionName,
               const std::string_view keyName,
               const std::string& rawValue,
               const std::string& defaultValue,
               const std::string& reasonText) {
        calls.push_back(RecordedCall{
            .sectionName = std::string{sectionName},
            .keyName = std::string{keyName},
            .rawValue = rawValue,
            .defaultValue = defaultValue,
            .reasonText = reasonText});
    };
}

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
    std::vector<RecordedCall> probeCalls{};
    document.addWarningHandler(makeProbe(probeCalls));

    const auto parsed = document.readUnsigned("mqtt", "port", 1U, 65535U, "1883");
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == 1884U);

    const auto missingValue = document.readUnsigned("mqtt", "missing", 1U, 65535U, "1883");
    REQUIRE_FALSE(missingValue.has_value());

    REQUIRE(probeCalls.empty());

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("ini_document_reports_invalid_unsigned_field", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[mqtt]\n"
        "port = invalid\n");

    const auto document = yaha::IniDocument::loadFromFile(iniPath);
    std::vector<RecordedCall> probeCalls{};
    document.addWarningHandler(makeProbe(probeCalls));

    const auto parsed = document.readUnsigned("mqtt", "port", 1U, 65535U, "1883");
    REQUIRE_FALSE(parsed.has_value());

    REQUIRE(probeCalls.size() == 1U);
    const auto& call = probeCalls.front();
    REQUIRE(call.sectionName == "mqtt");
    REQUIRE(call.keyName == "port");
    REQUIRE(call.rawValue == "invalid");
    REQUIRE(call.defaultValue == "1883");
    REQUIRE(call.reasonText == "invalid unsigned value for 'mqtt.port' (expected 1..65535, got 'invalid')");

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("ini_document_reads_optional_bool_field", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[mqtt]\n"
        "enabled = yes\n");

    const auto document = yaha::IniDocument::loadFromFile(iniPath);
    std::vector<RecordedCall> probeCalls{};
    document.addWarningHandler(makeProbe(probeCalls));

    const auto parsed = document.readBool("mqtt", "enabled", false);
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed);

    const auto missingValue = document.readBool("mqtt", "missing", false);
    REQUIRE_FALSE(missingValue.has_value());

    REQUIRE(probeCalls.empty());

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("ini_document_reports_invalid_bool_field", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[mqtt]\n"
        "enabled = maybe\n");

    const auto document = yaha::IniDocument::loadFromFile(iniPath);
    std::vector<RecordedCall> probeCalls{};
    document.addWarningHandler(makeProbe(probeCalls));

    const auto parsed = document.readBool("mqtt", "enabled", true);
    REQUIRE_FALSE(parsed.has_value());

    REQUIRE(probeCalls.size() == 1U);
    const auto& call = probeCalls.front();
    REQUIRE(call.sectionName == "mqtt");
    REQUIRE(call.keyName == "enabled");
    REQUIRE(call.rawValue == "maybe");
    REQUIRE(call.defaultValue == "true");
    REQUIRE(call.reasonText == "invalid boolean value for 'mqtt.enabled' (got 'maybe')");

    removeDirectoryQuiet(tempDir);
}
