#include <catch2/catch_test_macros.hpp>

#include "yaha/message_store_client/message_store_client_app.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr double k_upper_bound_factor{1.7};
constexpr double k_lower_bound_factor{0.6};

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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("load_config_parses_tree_compression_parameters", "[message_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[mqtt]\n"
        "host = 127.0.0.1\n"
        "\n"
        "[tree]\n"
        "maxHistoryLength = 70\n"
        "historyHysterese = 9\n"
        "maxValuesPerHistoryEntry = 77\n"
        "lengthForFurtherCompression = 8\n"
        "upperBoundFactor = 1.7\n"
        "upperBoundAddInMilliseconds = 2500\n"
        "lowerBoundFactor = 0.6\n"
        "lowerBoundSubInMilliseconds = 1500\n");

    yaha::MessageStoreClientRuntimeConfig config{};
    std::string errorMessage{};

    REQUIRE(tryLoadRuntimeConfigFromFile(configPath, config, errorMessage));
    REQUIRE(config.storeConfig.treeConfig.maxHistoryLength == 70U);
    REQUIRE(config.storeConfig.treeConfig.historyHysterese == 9U);
    REQUIRE(config.storeConfig.treeConfig.maxValuesPerHistoryEntry == 77U);
    REQUIRE(config.storeConfig.treeConfig.lengthForFurtherCompression == 8U);
    REQUIRE(config.storeConfig.treeConfig.upperBoundFactor == k_upper_bound_factor);
    REQUIRE(config.storeConfig.treeConfig.upperBoundAddInMilliseconds == 2500U);
    REQUIRE(config.storeConfig.treeConfig.lowerBoundFactor == k_lower_bound_factor);
    REQUIRE(config.storeConfig.treeConfig.lowerBoundSubInMilliseconds == 1500U);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_rejects_invalid_tree_factor_values", "[message_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[mqtt]\n"
        "host = 127.0.0.1\n"
        "\n"
        "[tree]\n"
        "upperBoundFactor = not-a-number\n");

    yaha::MessageStoreClientRuntimeConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromFile(configPath, config, errorMessage));
    REQUIRE(errorMessage.find("tree.upperBoundFactor") != std::string::npos);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_rejects_out_of_range_tree_factor_values", "[message_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[mqtt]\n"
        "host = 127.0.0.1\n"
        "\n"
        "[tree]\n"
        "upperBoundFactor = 1001\n");

    yaha::MessageStoreClientRuntimeConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromFile(configPath, config, errorMessage));
    REQUIRE(errorMessage.find("tree.upperBoundFactor") != std::string::npos);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_rejects_subscription_unknown_key", "[message_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[mqtt]\n"
        "host = 127.0.0.1\n"
        "\n"
        "[subscription]\n"
        "topic = #\n"
        "foo = bar\n");

    yaha::MessageStoreClientRuntimeConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromFile(configPath, config, errorMessage));
    REQUIRE(errorMessage.find("unknown key") != std::string::npos);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_rejects_subscription_qos_without_topic", "[message_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[mqtt]\n"
        "host = 127.0.0.1\n"
        "\n"
        "[subscription]\n"
        "qos = 1\n");

    yaha::MessageStoreClientRuntimeConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromFile(configPath, config, errorMessage));
    REQUIRE(errorMessage.find("preceding subscription.topic") != std::string::npos);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_rejects_subscription_topic_without_qos", "[message_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[mqtt]\n"
        "host = 127.0.0.1\n"
        "\n"
        "[subscription]\n"
        "topic = home/state\n");

    yaha::MessageStoreClientRuntimeConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromFile(configPath, config, errorMessage));
    REQUIRE(errorMessage.find("missing subscription.qos") != std::string::npos);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_rejects_empty_subscription_topic", "[message_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[mqtt]\n"
        "host = 127.0.0.1\n"
        "\n"
        "[subscription]\n"
        "topic = \n"
        "qos = 1\n");

    yaha::MessageStoreClientRuntimeConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromFile(configPath, config, errorMessage));
    REQUIRE(errorMessage.find("must not be empty") != std::string::npos);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("load_config_parses_server_host_when_set", "[message_store_client]") {
    const auto tempDir = makeTempDirectory();
    const auto configPath = writeConfigFile(tempDir,
        "[mqtt]\n"
        "host = 127.0.0.1\n"
        "\n"
        "[server]\n"
        "host = 0.0.0.0\n");

    yaha::MessageStoreClientRuntimeConfig config{};
    std::string errorMessage{};

    REQUIRE(tryLoadRuntimeConfigFromFile(configPath, config, errorMessage));
    REQUIRE(config.storeConfig.serverHost == "0.0.0.0");

    removeDirectoryQuiet(tempDir);
}

