#include <catch2/catch_test_macros.hpp>

#include "yaha/message_store_client/message_store_client_app.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path makeTempDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_msgstore_client_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void removeDirectoryQuiet(const std::filesystem::path& path) {
    std::error_code error{};
    std::filesystem::remove_all(path, error);
}

std::filesystem::path writeConfigFile(const std::filesystem::path& directory,
                                      const std::string& content) {
    const auto path = directory / "msgstore.ini";
    std::ofstream output{path};
    output << content;
    return path;
}

bool tryLoadRuntimeConfigFromFile(const std::filesystem::path& configPath,
                                  yaha::MessageStoreClientRuntimeConfig& output,
                                  std::string& errorMessage) {
    auto document = yaha::IniDocument{};
    try {
        document = yaha::IniDocument::loadFromFile(configPath);
    } catch (const std::exception& exceptionValue) {
        errorMessage = exceptionValue.what();
        return false;
    }

    return yaha::tryLoadMessageStoreClientRuntimeConfigFromIni(document, output, errorMessage);
}

} // namespace
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("load_config_parses_mqtt_server_persist_and_subscriptions", "[message_store_client]") {
    const auto tempDir = makeTempDirectory();

    const auto configPath = writeConfigFile(tempDir,
        "[mqtt]\n"
        "host = broker.local\n"
        "port = 1884\n"
        "clientId = msgstore-client\n"
        "reconnectDelayMs = 1234\n"
        "keepAliveIntervalMs = 5678\n"
        "loopSleepMs = 33\n"
        "\n"
        "[server]\n"
        "port = 9000\n"
        "path = /store\n"
        "\n"
        "[persist]\n"
        "directory = data-store\n"
        "filename = snapshot\n"
        "intervalMs = 250\n"
        "keepFiles = 7\n"
        "\n"
        "[messagestore]\n"
        "cleanupTopic = $MONITORING/messages/cleanup\n"
        "\n"
        "[subscription]\n"
        "topic = #\n"
        "qos = 1\n"
        "[subscription]\n"
        "topic = home/+/state\n"
        "qos = 0\n");

    yaha::MessageStoreClientRuntimeConfig config{};
    std::string errorMessage{};

    REQUIRE(tryLoadRuntimeConfigFromFile(configPath, config, errorMessage));
    REQUIRE(config.mqttConfig.brokerHost == "broker.local");
    REQUIRE(config.mqttConfig.brokerPort == 1884U);
    REQUIRE(config.mqttConfig.clientId == "msgstore-client");
    REQUIRE(config.mqttConfig.reconnectDelay.count() == 1234);
    REQUIRE(config.mqttConfig.keepAliveInterval.count() == 5678);
    REQUIRE(config.mqttConfig.loopSleep.count() == 33);

    REQUIRE(config.storeConfig.serverPort == 9000U);
    REQUIRE(config.storeConfig.serverPath == "/store");
    REQUIRE(config.storeConfig.persistenceConfig.directory == std::filesystem::path{"data-store"});
    REQUIRE(config.storeConfig.persistenceConfig.filename == "snapshot");
    REQUIRE(config.storeConfig.persistenceConfig.intervalMs == 250U);
    REQUIRE(config.storeConfig.persistenceConfig.keepFiles == 7U);
    REQUIRE(config.storeConfig.subscriptions.size() == 2U);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_uses_default_subscription_when_missing", "[message_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[mqtt]\n"
        "host = 127.0.0.1\n");

    yaha::MessageStoreClientRuntimeConfig config{};
    std::string errorMessage{};

    REQUIRE(tryLoadRuntimeConfigFromFile(configPath, config, errorMessage));
    REQUIRE(config.storeConfig.subscriptions.size() == 1U);
    REQUIRE(config.storeConfig.subscriptions.count("#") == 1U);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_rejects_invalid_subscription_qos", "[message_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[subscription]\n"
        "topic = #\n"
        "qos = 9\n");

    yaha::MessageStoreClientRuntimeConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromFile(configPath, config, errorMessage));
    REQUIRE_FALSE(errorMessage.empty());

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_rejects_legacy_subscriptions_section", "[message_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[subscriptions]\n"
        "# = 1\n");

    yaha::MessageStoreClientRuntimeConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromFile(configPath, config, errorMessage));
    REQUIRE(errorMessage.find("legacy section 'subscriptions'") != std::string::npos);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_rejects_invalid_numeric_fields", "[message_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[mqtt]\n"
        "port = abc\n");

    yaha::MessageStoreClientRuntimeConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromFile(configPath, config, errorMessage));
    REQUIRE(errorMessage.find("mqtt.port") != std::string::npos);

    removeDirectoryQuiet(tempDir);
}

