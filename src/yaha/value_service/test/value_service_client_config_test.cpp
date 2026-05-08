#include <catch2/catch_test_macros.hpp>

#include "yaha/value_service_client/value_service_client_app.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path writeTempIni(const std::string& content) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_value_service_runtime_test_" + std::to_string(stamp) + ".ini");
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output << content;
    return path;
}

bool tryLoadRuntimeConfigFromIniText(
    const std::string& iniText,
    yaha::ValueServiceClientRuntimeConfig& output,
    std::string& errorMessage) {
    const auto iniPath = writeTempIni(iniText);

    yaha::IniDocument document{};
    try {
        document = yaha::IniDocument::loadFromFile(iniPath);
    } catch (const std::exception& exceptionValue) {
        errorMessage = exceptionValue.what();
        std::filesystem::remove(iniPath);
        return false;
    }

    const bool success = yaha::tryLoadValueServiceClientRuntimeConfigFromIni(document, output, errorMessage);
    std::filesystem::remove(iniPath);
    return success;
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("value_service_runtime_config_parses_all_sections", "[value_service]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "port=1883\n"
        "clientId=value-service-client\n"
        "reconnectDelayMs=1200\n"
        "keepAliveIntervalMs=22\n"
        "loopSleepMs=7\n"
        "\n"
        "[filestore]\n"
        "use=true\n"
        "host=127.0.0.2\n"
        "port=8211\n"
        "filename=/values/from/filestore\n"
        "topicPrefix=$MONITOR/FS\n"
        "\n"
        "[valueservice]\n"
        "subscribeQoS=2\n"
        "valuesFileName=legacy.json\n";

    yaha::ValueServiceClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE(tryLoadRuntimeConfigFromIniText(iniText, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.empty());

    REQUIRE(runtimeConfig.mqttConfig.brokerHost == "127.0.0.1");
    REQUIRE(runtimeConfig.mqttConfig.brokerPort == 1883U);
    REQUIRE(runtimeConfig.mqttConfig.clientId == "value-service-client");

    REQUIRE(runtimeConfig.valueServiceConfig.fileStoreEnabled);
    REQUIRE(runtimeConfig.valueServiceConfig.fileStoreHost == "127.0.0.2");
    REQUIRE(runtimeConfig.valueServiceConfig.fileStorePort == 8211U);
    REQUIRE(runtimeConfig.valueServiceConfig.valuesKeyPath == "/values/from/filestore");
    REQUIRE(runtimeConfig.valueServiceConfig.monitorTopicPrefix == "$MONITOR/FS");
    REQUIRE(runtimeConfig.valueServiceConfig.subscribeQos == yaha::Qos::ExactlyOnce);
    REQUIRE(runtimeConfig.valueServiceConfig.legacyValuesFileName == "legacy.json");
}

TEST_CASE("value_service_runtime_config_rejects_invalid_subscribe_qos", "[value_service]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "\n"
        "[valueservice]\n"
        "subscribeQoS=9\n";

    yaha::ValueServiceClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromIniText(iniText, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.find("valueservice.subscribeQoS") != std::string::npos);
}

TEST_CASE("value_service_runtime_config_rejects_invalid_filestore_use", "[value_service]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "\n"
        "[filestore]\n"
        "use=maybe\n";

    yaha::ValueServiceClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromIniText(iniText, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.find("filestore.use") != std::string::npos);
}
