#include <catch2/catch_test_macros.hpp>

#include "yaha/remote_service_client/remote_service_client_app.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path writeTempIni(const std::string& content) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_remote_service_runtime_test_" + std::to_string(stamp) + ".ini");
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output << content;
    return path;
}

bool tryLoadRuntimeConfigFromIniText(
    const std::string& iniText,
    yaha::RemoteServiceClientRuntimeConfig& output,
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

    const bool success = yaha::tryLoadRemoteServiceClientRuntimeConfigFromIni(document, output, errorMessage);
    std::filesystem::remove(iniPath);
    return success;
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("remote_service_runtime_config_parses_all_sections", "[remote_service]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "port=1884\n"
        "clientId=remote-service-client\n"
        "\n"
        "[filestore]\n"
        "host=127.0.0.2\n"
        "port=8220\n"
        "filename=/remoteservice/mapping\n"
        "topicPrefix=$MONITOR/REMOTE_FS\n"
        "\n"
        "[remoteservice]\n"
        "listenHost=0.0.0.0\n"
        "listenPort=9133\n"
        "subscribeQoS=2\n";

    yaha::RemoteServiceClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE(tryLoadRuntimeConfigFromIniText(iniText, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.empty());

    REQUIRE(runtimeConfig.mqttConfig.brokerHost == "127.0.0.1");
    REQUIRE(runtimeConfig.mqttConfig.brokerPort == 1884U);
    REQUIRE(runtimeConfig.mqttConfig.clientId == "remote-service-client");

    REQUIRE(runtimeConfig.remoteServiceConfig.listenHost == "0.0.0.0");
    REQUIRE(runtimeConfig.remoteServiceConfig.listenPort == 9133U);
    REQUIRE(runtimeConfig.remoteServiceConfig.subscribeQos == yaha::Qos::ExactlyOnce);
    REQUIRE(runtimeConfig.remoteServiceConfig.fileStoreHost == "127.0.0.2");
    REQUIRE(runtimeConfig.remoteServiceConfig.fileStorePort == 8220U);
    REQUIRE(runtimeConfig.remoteServiceConfig.mappingKeyPath == "/remoteservice/mapping");
    REQUIRE(runtimeConfig.remoteServiceConfig.monitorTopicPrefix == "$MONITOR/REMOTE_FS");
}

TEST_CASE("remote_service_runtime_config_rejects_missing_mapping_key_path", "[remote_service]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "\n"
        "[filestore]\n"
        "host=127.0.0.2\n"
        "port=8220\n";

    yaha::RemoteServiceClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromIniText(iniText, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.find("filestore.filename") != std::string::npos);
}

TEST_CASE("remote_service_runtime_config_rejects_missing_filestore_host", "[remote_service]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "\n"
        "[filestore]\n"
        "port=8220\n"
        "filename=/remoteservice/mapping\n";

    yaha::RemoteServiceClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromIniText(iniText, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.find("filestore.host") != std::string::npos);
}

TEST_CASE("remote_service_runtime_config_rejects_invalid_subscribe_qos", "[remote_service]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "\n"
        "[filestore]\n"
        "host=127.0.0.2\n"
        "port=8220\n"
        "filename=/remoteservice/mapping\n"
        "\n"
        "[remoteservice]\n"
        "subscribeQoS=7\n";

    yaha::RemoteServiceClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromIniText(iniText, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.find("remoteservice.subscribeQoS") != std::string::npos);
}

TEST_CASE("remote_service_runtime_config_rejects_invalid_filestore_port", "[remote_service]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "\n"
        "[filestore]\n"
        "host=127.0.0.2\n"
        "port=70000\n"
        "filename=/remoteservice/mapping\n";

    yaha::RemoteServiceClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE_FALSE(tryLoadRuntimeConfigFromIniText(iniText, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.find("filestore.port") != std::string::npos);
}