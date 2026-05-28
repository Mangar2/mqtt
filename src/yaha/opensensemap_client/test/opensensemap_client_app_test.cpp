#include "yaha/opensensemap_client/opensensemap_client_app.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

class ScopedIniFile {
public:
    explicit ScopedIniFile(const std::string& content)
        : path_(std::filesystem::temp_directory_path() / "opensensemap_client_app_test.ini") {
        std::ofstream output{path_};
        output << content;
    }

    ~ScopedIniFile() {
        std::error_code errorCode{};
        std::filesystem::remove(path_, errorCode);
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("load_runtime_config_parses_opensensemap_and_sensor_sections", "[opensensemap_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "host = 127.0.0.1\n"
        "port = 1883\n"
        "clientId = yahaopensensemapclient\n"
        "\n"
        "[opensensemap]\n"
        "id = box-abc\n"
        "host = ingress.opensensemap.org\n"
        "port = 443\n"
        "qos = 1\n"
        "useTls = true\n"
        "\n"
        "[sensor]\n"
        "name = temperature\n"
        "unit = C\n"
        "topic = house/living/temperature\n"
        "id = sensor-temp\n"
        "\n"
        "[sensor]\n"
        "name = humidity\n"
        "unit = %\n"
        "topic = house/living/humidity\n"
        "id = sensor-humidity\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::OpenSenseMapClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE(yaha::tryLoadOpenSenseMapClientRuntimeConfigFromIni(document, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeConfig.openSenseMapConfig.boxIdentifier == "box-abc");
    REQUIRE(runtimeConfig.openSenseMapConfig.sensors.size() == 2U);
    REQUIRE(runtimeConfig.openSenseMapConfig.sensors[0].sensorName == "temperature");
    REQUIRE(runtimeConfig.openSenseMapConfig.sensors[0].sensorUnit == "C");
    REQUIRE(runtimeConfig.openSenseMapConfig.sensors[0].topicFilter == "house/living/temperature");
    REQUIRE(runtimeConfig.openSenseMapConfig.sensors[0].sensorIdentifier == "sensor-temp");
}

TEST_CASE("load_config_rejects_legacy_uint_sensor_key", "[opensensemap_client]") {
    const std::string iniText =
        "[opensensemap]\n"
        "id = box-abc\n"
        "\n"
        "[sensor]\n"
        "name = temperature\n"
        "uint = C\n"
        "topic = house/living/temperature\n"
        "id = sensor-temp\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::OpenSenseMapConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryLoadOpenSenseMapConfigFromIni(document, config, errorMessage));
    REQUIRE(errorMessage.find("use 'unit'") != std::string::npos);
}

TEST_CASE("load_config_requires_sensor_entries", "[opensensemap_client]") {
    const std::string iniText =
        "[opensensemap]\n"
        "id = box-abc\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::OpenSenseMapConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryLoadOpenSenseMapConfigFromIni(document, config, errorMessage));
    REQUIRE(errorMessage.find("missing [sensor]") != std::string::npos);
}

TEST_CASE("load_config_requires_opensensemap_id", "[opensensemap_client]") {
    const std::string iniText =
        "[opensensemap]\n"
        "host = ingress.opensensemap.org\n"
        "\n"
        "[sensor]\n"
        "name = temperature\n"
        "unit = C\n"
        "topic = house/living/temperature\n"
        "id = sensor-temp\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::OpenSenseMapConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryLoadOpenSenseMapConfigFromIni(document, config, errorMessage));
    REQUIRE(errorMessage == "opensensemap.id must not be empty");
}
