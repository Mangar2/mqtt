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

    yaha::IniDocument document{};
    std::string errorMessage{};

    REQUIRE(yaha::IniDocument::tryLoadFromFile(iniPath, document, errorMessage));
    REQUIRE(document.lastValue("mqtt", "host").value_or("") == "broker.local");
    REQUIRE(document.lastValue("mqtt", "port").value_or("") == "1883");
    REQUIRE(document.lastValue("server", "path").value_or("") == "/store");

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_ini_preserves_multivalue_keys", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[tracing]\n"
        "module = broker\n"
        "module = connection\n"
        "module = monitoring\n");

    yaha::IniDocument document{};
    std::string errorMessage{};

    REQUIRE(yaha::IniDocument::tryLoadFromFile(iniPath, document, errorMessage));

    const auto* section = document.findSection("tracing");
    REQUIRE(section != nullptr);
    const auto values = section->valuesForKey("module");
    REQUIRE(values.has_value());
    REQUIRE(values->size() == 3U);
    REQUIRE((*values)[0] == "broker");
    REQUIRE((*values)[1] == "connection");
    REQUIRE((*values)[2] == "monitoring");
    REQUIRE(section->lastValueForKey("module").value_or("") == "monitoring");

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_ini_rejects_missing_equals", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[mqtt]\n"
        "host broker.local\n");

    yaha::IniDocument document{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::IniDocument::tryLoadFromFile(iniPath, document, errorMessage));
    REQUIRE(errorMessage.find("missing '='") != std::string::npos);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_ini_rejects_empty_section_name", "[ini]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[]\n"
        "host = broker.local\n");

    yaha::IniDocument document{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::IniDocument::tryLoadFromFile(iniPath, document, errorMessage));
    REQUIRE(errorMessage.find("empty section name") != std::string::npos);

    removeDirectoryQuiet(tempDir);
}
