#include "yaha/opensensemap_client/opensensemap_client_app.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
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

class ScopedPathPrefix {
public:
    explicit ScopedPathPrefix(const std::filesystem::path& prefixPath)
        : hadPreviousPath_(std::getenv("PATH") != nullptr)
        , previousPath_(hadPreviousPath_ ? std::getenv("PATH") : "") {
        const std::string newPath = hadPreviousPath_
            ? (prefixPath.string() + ":" + previousPath_)
            : prefixPath.string();
        setenv("PATH", newPath.c_str(), 1);
    }

    ~ScopedPathPrefix() {
        if (hadPreviousPath_) {
            setenv("PATH", previousPath_.c_str(), 1);
            return;
        }
        unsetenv("PATH");
    }

private:
    bool hadPreviousPath_{false};
    std::string previousPath_{};
};

[[nodiscard]] std::filesystem::path makeFakeCurlDirectory(const std::string& scriptBody) {
    const auto stamp = std::to_string(
        static_cast<long long>(std::filesystem::file_time_type::clock::now().time_since_epoch().count()));
    const auto directoryPath = std::filesystem::temp_directory_path() / ("opensensemap_curl_stub_" + stamp);
    std::filesystem::create_directories(directoryPath);

    const auto scriptPath = directoryPath / "curl";
    std::ofstream scriptFile{scriptPath};
    scriptFile << "#!/bin/sh\n";
    scriptFile << scriptBody;
    scriptFile << "\n";
    scriptFile.close();

    std::filesystem::permissions(
        scriptPath,
        std::filesystem::perms::owner_exec |
            std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);

    return directoryPath;
}

void removeDirectoryQuiet(const std::filesystem::path& directoryPath) {
    std::error_code errorCode{};
    std::filesystem::remove_all(directoryPath, errorCode);
}

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

TEST_CASE("load_config_rejects_sensor_fields_without_name", "[opensensemap_client]") {
    const std::string iniText =
        "[opensensemap]\n"
        "id = box-abc\n"
        "\n"
        "[sensor]\n"
        "unit = C\n"
        "topic = house/living/temperature\n"
        "id = sensor-temp\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::OpenSenseMapConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryLoadOpenSenseMapConfigFromIni(document, config, errorMessage));
    REQUIRE(errorMessage.find("preceding sensor.name") != std::string::npos);
}

TEST_CASE("load_config_rejects_duplicate_sensor_topic", "[opensensemap_client]") {
    const std::string iniText =
        "[opensensemap]\n"
        "id = box-abc\n"
        "\n"
        "[sensor]\n"
        "name = temperature\n"
        "unit = C\n"
        "topic = house/living/temperature\n"
        "topic = house/living/temperature2\n"
        "id = sensor-temp\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::OpenSenseMapConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryLoadOpenSenseMapConfigFromIni(document, config, errorMessage));
    REQUIRE(errorMessage.find("duplicate sensor.topic") != std::string::npos);
}

TEST_CASE("load_config_rejects_incomplete_sensor_entry", "[opensensemap_client]") {
    const std::string iniText =
        "[opensensemap]\n"
        "id = box-abc\n"
        "\n"
        "[sensor]\n"
        "name = temperature\n"
        "unit = C\n"
        "topic = house/living/temperature\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::OpenSenseMapConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryLoadOpenSenseMapConfigFromIni(document, config, errorMessage));
    REQUIRE(errorMessage.find("incomplete [sensor] entry") != std::string::npos);
}

TEST_CASE("load_runtime_config_falls_back_on_invalid_mqtt_values", "[opensensemap_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "loopSleepMs = 0\n"
        "\n"
        "[opensensemap]\n"
        "id = box-abc\n"
        "\n"
        "[sensor]\n"
        "name = temperature\n"
        "unit = C\n"
        "topic = house/living/temperature\n"
        "id = sensor-temp\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::OpenSenseMapClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE(yaha::tryLoadOpenSenseMapClientRuntimeConfigFromIni(document, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeConfig.mqttConfig.loopSleep == std::chrono::milliseconds{20});
}

TEST_CASE("opensensemap_request_sender_parses_successful_curl_output", "[opensensemap_client]") {
    const auto fakeCurlDirectory = makeFakeCurlDirectory(
        "printf '{\"message\":\"created\"}\\n__YAHA_STATUS__:201\\n__YAHA_CTYPE__:application/json\\n'\n"
        "exit 0");
    const ScopedPathPrefix scopedPath{fakeCurlDirectory};

    const yaha::OpenSenseMapConfig config{
        .boxIdentifier = "box-abc",
        .host = "example.org",
        .port = 443U,
        .useTls = true,
    };
    const auto sender = yaha::makeOpenSenseMapRequestSender(config);

    const yaha::OpenSenseMapHttpResult result = sender("/boxes/box-abc/sensor-1", "{\"value\":12}");
    REQUIRE(result.statusCode == 201);
    REQUIRE(result.payload.find("created") != std::string::npos);
    REQUIRE(result.contentType == "application/json");

    removeDirectoryQuiet(fakeCurlDirectory);
}

TEST_CASE("opensensemap_request_sender_throws_on_non_zero_curl_exit", "[opensensemap_client]") {
    const auto fakeCurlDirectory = makeFakeCurlDirectory("exit 2");
    const ScopedPathPrefix scopedPath{fakeCurlDirectory};

    const yaha::OpenSenseMapConfig config{
        .boxIdentifier = "box-abc",
        .host = "example.org",
        .port = 443U,
        .useTls = true,
    };
    const auto sender = yaha::makeOpenSenseMapRequestSender(config);

    REQUIRE_THROWS(sender("/boxes/box-abc/sensor-1", "{\"value\":12}"));

    removeDirectoryQuiet(fakeCurlDirectory);
}

TEST_CASE("opensensemap_request_sender_throws_on_missing_metadata", "[opensensemap_client]") {
    const auto fakeCurlDirectory = makeFakeCurlDirectory("printf 'body only'\nexit 0");
    const ScopedPathPrefix scopedPath{fakeCurlDirectory};

    const yaha::OpenSenseMapConfig config{
        .boxIdentifier = "box-abc",
        .host = "example.org",
        .port = 443U,
        .useTls = true,
    };
    const auto sender = yaha::makeOpenSenseMapRequestSender(config);

    REQUIRE_THROWS(sender("/boxes/box-abc/sensor-1", "{\"value\":12}"));

    removeDirectoryQuiet(fakeCurlDirectory);
}

TEST_CASE("load_config_rejects_unknown_sensor_key", "[opensensemap_client]") {
    const std::string iniText =
        "[opensensemap]\n"
        "id = box-abc\n"
        "\n"
        "[sensor]\n"
        "name = temperature\n"
        "unit = C\n"
        "topic = house/living/temperature\n"
        "id = sensor-temp\n"
        "foo = bar\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::OpenSenseMapConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryLoadOpenSenseMapConfigFromIni(document, config, errorMessage));
    REQUIRE(errorMessage.find("invalid key in [sensor]") != std::string::npos);
}

TEST_CASE("load_runtime_config_falls_back_on_invalid_opensensemap_fields", "[opensensemap_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "port = invalid\n"
        "\n"
        "[opensensemap]\n"
        "id = box-abc\n"
        "station = station-1\n"
        "port = invalid\n"
        "qos = 9\n"
        "useTls = maybe\n"
        "\n"
        "[sensor]\n"
        "name = temperature\n"
        "unit = C\n"
        "topic = house/living/temperature\n"
        "id = sensor-temp\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::OpenSenseMapClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE(yaha::tryLoadOpenSenseMapClientRuntimeConfigFromIni(document, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeConfig.openSenseMapConfig.stationName == "station-1");
    REQUIRE(runtimeConfig.openSenseMapConfig.port == 443U);
}

TEST_CASE("load_runtime_config_parses_opensensemap_message_logging_flags", "[opensensemap_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "logReason = false\n"
        "\n"
        "[opensensemap]\n"
        "id = box-abc\n"
        "logIncomingMessages = true\n"
        "logReason = true\n"
        "\n"
        "[sensor]\n"
        "name = temperature\n"
        "unit = C\n"
        "topic = house/living/temperature\n"
        "id = sensor-temp\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::OpenSenseMapClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE(yaha::tryLoadOpenSenseMapClientRuntimeConfigFromIni(document, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeConfig.logIncomingMessages);
    REQUIRE(runtimeConfig.mqttConfig.logReason);
}

TEST_CASE("load_runtime_config_falls_back_on_invalid_opensensemap_logging_flags", "[opensensemap_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "logReason = false\n"
        "\n"
        "[opensensemap]\n"
        "id = box-abc\n"
        "logIncomingMessages = maybe\n"
        "logReason = maybe\n"
        "\n"
        "[sensor]\n"
        "name = temperature\n"
        "unit = C\n"
        "topic = house/living/temperature\n"
        "id = sensor-temp\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::OpenSenseMapClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE(yaha::tryLoadOpenSenseMapClientRuntimeConfigFromIni(document, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.empty());
    REQUIRE_FALSE(runtimeConfig.logIncomingMessages);
    REQUIRE(runtimeConfig.mqttConfig.logReason);
}

TEST_CASE("load_runtime_config_ignores_mqtt_log_reason_for_opensensemap_logging", "[opensensemap_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "logReason = false\n"
        "\n"
        "[opensensemap]\n"
        "id = box-abc\n"
        "logIncomingMessages = true\n"
        "\n"
        "[sensor]\n"
        "name = temperature\n"
        "unit = C\n"
        "topic = house/living/temperature\n"
        "id = sensor-temp\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::OpenSenseMapClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE(yaha::tryLoadOpenSenseMapClientRuntimeConfigFromIni(document, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeConfig.logIncomingMessages);
    REQUIRE(runtimeConfig.mqttConfig.logReason);
}

TEST_CASE("opensensemap_request_sender_throws_on_invalid_status_metadata", "[opensensemap_client]") {
    const auto fakeCurlDirectory = makeFakeCurlDirectory(
        "printf '{\"message\":\"created\"}\\n__YAHA_STATUS__:abc\\n__YAHA_CTYPE__:application/json\\n'\n"
        "exit 0");
    const ScopedPathPrefix scopedPath{fakeCurlDirectory};

    const yaha::OpenSenseMapConfig config{
        .boxIdentifier = "box-abc",
        .host = "example.org",
        .port = 443U,
        .useTls = true,
    };
    const auto sender = yaha::makeOpenSenseMapRequestSender(config);

    REQUIRE_THROWS(sender("/boxes/box-abc/sensor-1", "{'value':12}"));

    removeDirectoryQuiet(fakeCurlDirectory);
}
