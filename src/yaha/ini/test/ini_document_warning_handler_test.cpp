#include <catch2/catch_test_macros.hpp>

#include "yaha/ini/ini_document.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path makeTempDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_ini_warning_handler_test_" + std::to_string(stamp));
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

struct RecordedCall {
    std::string serviceName{};
    std::string sectionName{};
    std::string keyName{};
    std::string rawValue{};
    std::string defaultValue{};
    std::string reasonText{};
};

yaha::ConfigWarningHandler makeProbe(std::vector<RecordedCall>& calls) {
    return [&calls](
               const std::string_view serviceName,
               const std::string_view sectionName,
               const std::string_view keyName,
               const std::string& rawValue,
               const std::string& defaultValue,
               const std::string& reasonText) {
        calls.push_back(RecordedCall{
            .serviceName = std::string{serviceName},
            .sectionName = std::string{sectionName},
            .keyName = std::string{keyName},
            .rawValue = rawValue,
            .defaultValue = defaultValue,
            .reasonText = reasonText});
    };
}

} // namespace

TEST_CASE("default_warning_handler_formats_fallback_message_on_stderr", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir, "[mqtt]\nport = 1883\n");
    const auto document = yaha::IniDocument::loadFromFile(iniPath, "svc");

    std::ostringstream capturedStderr{};
    auto* const originalBuffer = std::cerr.rdbuf(capturedStderr.rdbuf());
    document.reportFallback("sec", "key", "raw", "default", "reason");
    std::cerr.rdbuf(originalBuffer);

    REQUIRE(
        capturedStderr.str() ==
        "svc[warn] config_fallback section=sec key=key value='raw' default='default' "
        "reason='reason'\n");

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("add_warning_handler_invokes_all_registered_handlers", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir, "[mqtt]\nport = 1883\n");
    const auto document = yaha::IniDocument::loadFromFile(iniPath, "svc");

    std::vector<RecordedCall> firstCalls{};
    std::vector<RecordedCall> secondCalls{};
    document.addWarningHandler(makeProbe(firstCalls));
    document.addWarningHandler(makeProbe(secondCalls));

    std::ostringstream capturedStderr{};
    auto* const originalBuffer = std::cerr.rdbuf(capturedStderr.rdbuf());
    document.reportFallback("filestore", "port", "99999", "1", "out of range");
    std::cerr.rdbuf(originalBuffer);

    REQUIRE(firstCalls.size() == 1U);
    REQUIRE(secondCalls.size() == 1U);
    REQUIRE(firstCalls.front().sectionName == "filestore");
    REQUIRE(secondCalls.front().keyName == "port");

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("clear_warning_handlers_removes_default_handler", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir, "[mqtt]\nport = 1883\n");
    const auto document = yaha::IniDocument::loadFromFile(iniPath, "svc");

    document.clearWarningHandlers();
    std::vector<RecordedCall> probeCalls{};
    document.addWarningHandler(makeProbe(probeCalls));

    std::ostringstream capturedStderr{};
    auto* const originalBuffer = std::cerr.rdbuf(capturedStderr.rdbuf());
    document.reportFallback("sec", "key", "raw", "default", "reason");
    std::cerr.rdbuf(originalBuffer);

    REQUIRE(capturedStderr.str().empty());
    REQUIRE(probeCalls.size() == 1U);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("report_fallback_forwards_service_name_and_fields", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir, "[mqtt]\nport = 1883\n");
    const auto document = yaha::IniDocument::loadFromFile(iniPath, "myservice");

    std::vector<RecordedCall> probeCalls{};
    document.addWarningHandler(makeProbe(probeCalls));

    std::ostringstream capturedStderr{};
    auto* const originalBuffer = std::cerr.rdbuf(capturedStderr.rdbuf());
    document.reportFallback("filestore", "port", "99999", "1", "out of range");
    std::cerr.rdbuf(originalBuffer);

    REQUIRE(probeCalls.size() == 1U);
    const auto& call = probeCalls.front();
    REQUIRE(call.serviceName == "myservice");
    REQUIRE(call.sectionName == "filestore");
    REQUIRE(call.keyName == "port");
    REQUIRE(call.rawValue == "99999");
    REQUIRE(call.defaultValue == "1");
    REQUIRE(call.reasonText == "out of range");

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_from_file_defaults_service_name_when_omitted", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir, "[mqtt]\nport = 1883\n");
    const auto document = yaha::IniDocument::loadFromFile(iniPath);

    std::vector<RecordedCall> probeCalls{};
    document.addWarningHandler(makeProbe(probeCalls));

    std::ostringstream capturedStderr{};
    auto* const originalBuffer = std::cerr.rdbuf(capturedStderr.rdbuf());
    document.reportFallback("sec", "key", "raw", "default", "reason");
    std::cerr.rdbuf(originalBuffer);

    REQUIRE(probeCalls.size() == 1U);
    REQUIRE(probeCalls.front().serviceName.empty());

    removeDirectoryQuiet(tempDir);
}
