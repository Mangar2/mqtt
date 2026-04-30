#include <catch2/catch_test_macros.hpp>

#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/mqtt_client_config.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path makeTempDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_mqtt_config_test_" + std::to_string(stamp));
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

bool loadDocument(const std::filesystem::path& path,
                  yaha::IniDocument& document,
                  std::string& errorMessage) {
    return yaha::IniDocument::tryLoadFromFile(path, document, errorMessage);
}

} // namespace

TEST_CASE("mqtt_client_config_maps_optional_mqtt_fields", "[mqtt_client]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[mqtt]\n"
        "host = broker.local\n"
        "port = 1884\n"
        "clientId = my-client\n"
        "reconnectDelayMs = 1234\n"
        "keepAliveIntervalMs = 4321\n"
        "loopSleepMs = 25\n");

    yaha::IniDocument document{};
    std::string errorMessage{};
    REQUIRE(loadDocument(iniPath, document, errorMessage));

    yaha::YahaMqttClient::Config config{};
    REQUIRE(yaha::tryLoadMqttClientConfigFromIni(document, config, errorMessage));
    REQUIRE(config.brokerHost == "broker.local");
    REQUIRE(config.brokerPort == 1884U);
    REQUIRE(config.clientId == "my-client");
    REQUIRE(config.reconnectDelay.count() == 1234);
    REQUIRE(config.keepAliveInterval.count() == 4321);
    REQUIRE(config.loopSleep.count() == 25);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("mqtt_client_config_rejects_invalid_numeric_values", "[mqtt_client]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[mqtt]\n"
        "port = abc\n");

    yaha::IniDocument document{};
    std::string errorMessage{};
    REQUIRE(loadDocument(iniPath, document, errorMessage));

    yaha::YahaMqttClient::Config config{};
    REQUIRE_FALSE(yaha::tryLoadMqttClientConfigFromIni(document, config, errorMessage));
    REQUIRE(errorMessage == "invalid mqtt.port");

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("mqtt_client_subscription_parser_reads_topic_qos_map", "[mqtt_client]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[subscriptions]\n"
        "# = 1\n"
        "home/+/state = 0\n");

    yaha::IniDocument document{};
    std::string errorMessage{};
    REQUIRE(loadDocument(iniPath, document, errorMessage));

    yaha::SubscriptionMap subscriptions{};
    REQUIRE(yaha::tryLoadSubscriptionsFromIni(document, "subscriptions", subscriptions, errorMessage));
    REQUIRE(subscriptions.size() == 2U);
    REQUIRE(subscriptions.at("#") == yaha::Qos::AtLeastOnce);
    REQUIRE(subscriptions.at("home/+/state") == yaha::Qos::AtMostOnce);

    removeDirectoryQuiet(tempDir);
}

TEST_CASE("mqtt_client_subscription_parser_rejects_invalid_qos", "[mqtt_client]") {
    const auto tempDir = makeTempDirectory();
    const auto iniPath = writeIniFile(tempDir,
        "[subscriptions]\n"
        "# = 9\n");

    yaha::IniDocument document{};
    std::string errorMessage{};
    REQUIRE(loadDocument(iniPath, document, errorMessage));

    yaha::SubscriptionMap subscriptions{};
    REQUIRE_FALSE(yaha::tryLoadSubscriptionsFromIni(document, "subscriptions", subscriptions, errorMessage));
    REQUIRE(errorMessage.find("invalid qos") != std::string::npos);

    removeDirectoryQuiet(tempDir);
}
