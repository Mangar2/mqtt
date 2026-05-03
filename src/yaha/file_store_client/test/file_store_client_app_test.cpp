#include <catch2/catch_test_macros.hpp>

#include "yaha/file_store_client/file_store_client_app.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path makeTempDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_filestore_client_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void removeDirectoryQuiet(const std::filesystem::path& path) {
    std::error_code error{};
    std::filesystem::remove_all(path, error);
}

std::filesystem::path writeConfigFile(const std::filesystem::path& directory,
                                      const std::string& content) {
    const auto path = directory / "filestore.ini";
    std::ofstream output{path};
    output << content;
    return path;
}

[[nodiscard]] yaha::FileStoreClientRuntimeConfigLoadResult
loadRuntimeConfigFromFile(const std::filesystem::path& configPath) {
    auto document = yaha::IniDocument{};
    try {
        document = yaha::IniDocument::loadFromFile(configPath);
    } catch (const std::exception& exceptionValue) {
        return yaha::FileStoreClientRuntimeConfigLoadResult{
            false,
            {},
            exceptionValue.what()};
    }

    return yaha::loadFileStoreClientRuntimeConfigFromIni(document);
}

} // namespace

TEST_CASE("load_config_parses_mqtt_filestore_and_monitoring_sections", "[file_store_client]") {
    const auto tempDir = makeTempDirectory();

    const auto configPath = writeConfigFile(tempDir,
        "[mqtt]\n"
        "host = broker.local\n"
        "port = 1884\n"
        "clientId = filestore-client\n"
        "reconnectDelayMs = 1234\n"
        "keepAliveIntervalMs = 5678\n"
        "loopSleepMs = 33\n"
        "\n"
        "[server]\n"
        "host = 0.0.0.0\n"
        "port = 8211\n"
        "\n"
        "[filestore]\n"
        "directory = data-store\n"
        "keepFiles = 5\n"
        "maxKeyLength = 120\n"
        "\n"
        "[monitoring]\n"
        "enabled = true\n"
        "topicPrefix = $MONITOR/FileStore\n"
        "qos = 2\n"
        "retain = true\n"
        "watchIntervalMs = 250\n");

    const auto loadResult = loadRuntimeConfigFromFile(configPath);
    REQUIRE(loadResult.success);
    const auto& config = loadResult.config;
    REQUIRE(config.mqttConfig.brokerHost == "broker.local");
    REQUIRE(config.mqttConfig.brokerPort == 1884U);
    REQUIRE(config.mqttConfig.clientId == "filestore-client");
    REQUIRE(config.mqttConfig.reconnectDelay.count() == 1234);
    REQUIRE(config.mqttConfig.keepAliveInterval.count() == 5678);
    REQUIRE(config.mqttConfig.loopSleep.count() == 33);

    REQUIRE(config.storeConfig.serverHost == "0.0.0.0");
    REQUIRE(config.storeConfig.serverPort == 8211U);
    REQUIRE(config.storeConfig.directory == std::filesystem::path{"data-store"});
    REQUIRE(config.storeConfig.keepFiles == 5U);
    REQUIRE(config.storeConfig.maxKeyLength == 120U);
    REQUIRE(config.storeConfig.monitoring.enabled);
    REQUIRE(config.storeConfig.monitoring.topicPrefix == "$MONITOR/FileStore");
    REQUIRE(config.storeConfig.monitoring.qos == yaha::Qos::ExactlyOnce);
    REQUIRE(config.storeConfig.monitoring.retain);
    REQUIRE(config.storeConfig.monitoring.watchIntervalMs == 250U);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_uses_defaults_when_sections_missing", "[file_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[mqtt]\n"
        "host = 127.0.0.1\n");

    const auto loadResult = loadRuntimeConfigFromFile(configPath);
    REQUIRE(loadResult.success);
    const auto& config = loadResult.config;
    REQUIRE(config.storeConfig.serverPort == 8210U);
    REQUIRE(config.storeConfig.keepFiles == 2U);
    REQUIRE(config.storeConfig.maxKeyLength == 100U);
    REQUIRE(config.storeConfig.monitoring.enabled);
    REQUIRE(config.storeConfig.monitoring.qos == yaha::Qos::AtLeastOnce);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_rejects_invalid_monitoring_qos", "[file_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[monitoring]\n"
        "qos = 9\n");

    const auto loadResult = loadRuntimeConfigFromFile(configPath);
    REQUIRE_FALSE(loadResult.success);
    REQUIRE_FALSE(loadResult.errorMessage.empty());

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_rejects_invalid_bool_field", "[file_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[monitoring]\n"
        "retain = maybe\n");

    const auto loadResult = loadRuntimeConfigFromFile(configPath);
    REQUIRE_FALSE(loadResult.success);
    REQUIRE_FALSE(loadResult.errorMessage.empty());

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_rejects_invalid_server_port", "[file_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[server]\n"
        "port = invalid\n");

    const auto loadResult = loadRuntimeConfigFromFile(configPath);
    REQUIRE_FALSE(loadResult.success);
    REQUIRE_FALSE(loadResult.errorMessage.empty());

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_rejects_invalid_watch_interval", "[file_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[monitoring]\n"
        "watchIntervalMs = 9999999\n");

    const auto loadResult = loadRuntimeConfigFromFile(configPath);
    REQUIRE_FALSE(loadResult.success);
    REQUIRE_FALSE(loadResult.errorMessage.empty());

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_runtime_config_rejects_invalid_mqtt_port", "[file_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[mqtt]\n"
        "host = broker.local\n"
        "port = invalid\n"
        "\n"
        "[filestore]\n"
        "directory = data-store\n");

    const auto loadResult = loadRuntimeConfigFromFile(configPath);
    REQUIRE_FALSE(loadResult.success);
    REQUIRE_FALSE(loadResult.errorMessage.empty());

    removeDirectoryQuiet(tempDir);
}
