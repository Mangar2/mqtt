#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.h"
#include "yaha/ini/ini_document.h"

namespace {

std::filesystem::path writeTempIni(const std::string& content) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_http_mqtt_interface_client_test_" + std::to_string(stamp) + ".ini");
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output << content;
    return path;
}

} // namespace

TEST_CASE("load_http_mqtt_interface_client_config_defaults", "[http_mqtt_interface_client]") {
    const auto iniPath = writeTempIni("");
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::HttpMqttInterfaceClientConfig config{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadHttpMqttInterfaceClientConfigFromIni(
        document,
        config,
        errorMessage);

    REQUIRE(success);
    REQUIRE(errorMessage.empty());
    REQUIRE(config.listenerHost == "127.0.0.1");
    REQUIRE(config.listenerPort == 8092U);
    REQUIRE(config.enablePublishPhpAlias);
    REQUIRE_FALSE(config.useLegacyPhpResponse);

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_http_mqtt_interface_client_config_from_ini", "[http_mqtt_interface_client]") {
    const std::string iniText =
        "[httpMqttInterface]\n"
        "listenerHost=0.0.0.0\n"
        "listenerPort=8123\n"
        "enablePublishPhpAlias=false\n"
        "useLegacyPhpResponse=true\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::HttpMqttInterfaceClientConfig config{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadHttpMqttInterfaceClientConfigFromIni(
        document,
        config,
        errorMessage);

    REQUIRE(success);
    REQUIRE(errorMessage.empty());
    REQUIRE(config.listenerHost == "0.0.0.0");
    REQUIRE(config.listenerPort == 8123U);
    REQUIRE_FALSE(config.enablePublishPhpAlias);
    REQUIRE(config.useLegacyPhpResponse);

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_http_mqtt_interface_client_config_reports_invalid_port", "[http_mqtt_interface_client]") {
    const std::string iniText =
        "[httpMqttInterface]\n"
        "listenerPort=70000\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::HttpMqttInterfaceClientConfig config{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadHttpMqttInterfaceClientConfigFromIni(
        document,
        config,
        errorMessage);

    REQUIRE_FALSE(success);
    REQUIRE(errorMessage.find("httpMqttInterface.listenerPort") != std::string::npos);

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_http_mqtt_interface_client_config_reports_invalid_alias_flag", "[http_mqtt_interface_client]") {
    const std::string iniText =
        "[httpMqttInterface]\n"
        "enablePublishPhpAlias=maybe\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::HttpMqttInterfaceClientConfig config{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadHttpMqttInterfaceClientConfigFromIni(
        document,
        config,
        errorMessage);

    REQUIRE_FALSE(success);
    REQUIRE(errorMessage.find("httpMqttInterface.enablePublishPhpAlias") != std::string::npos);

    std::filesystem::remove(iniPath);
}
