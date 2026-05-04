#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "yaha/automation_client/automation_client_app.h"

namespace {

constexpr double k_test_longitude{8.68};
constexpr double k_test_latitude{50.11};

std::filesystem::path writeTempIni(const std::string& content) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_automation_client_test_" + std::to_string(stamp) + ".ini");
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output << content;
    return path;
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("load_automation_client_runtime_config_from_ini", "[automation_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "port=1883\n"
        "clientId=automation-client\n"
        "reconnectDelayMs=1000\n"
        "keepAliveIntervalMs=15\n"
        "loopSleepMs=10\n"
        "\n"
        "[filestore]\n"
        "use=true\n"
        "host=127.0.0.1\n"
        "port=8210\n"
        "path=/automation/rules\n"
        "\n"
        "[monitoring]\n"
        "topicPrefix=$MONITOR/FileStore\n"
        "\n"
        "[automation]\n"
        "topicPrefix=$MONITORING/automation\n"
        "managementTopicPrefix=$MONITORING/automation/rules\n"
        "longitude=8.68\n"
        "latitude=50.11\n"
        "subscribeQoS=1\n"
        "logIncomingMessages=true\n"
        "logOutgoingMessages=true\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::AutomationClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadAutomationClientRuntimeConfigFromIni(
        document,
        runtimeConfig,
        errorMessage);

    REQUIRE(success);
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeConfig.automationConfig.fileStoreEnabled);
    REQUIRE(runtimeConfig.automationConfig.fileStorePort == 8210U);
    REQUIRE(runtimeConfig.automationConfig.rulesKeyPath == "/automation/rules");
    REQUIRE(runtimeConfig.automationConfig.monitorTopicPrefix == "$MONITOR/FileStore");
    REQUIRE(runtimeConfig.automationConfig.automationTopicPrefix == "$MONITORING/automation");
    REQUIRE(runtimeConfig.automationConfig.managementTopicPrefix == "$MONITORING/automation/rules");
    REQUIRE(runtimeConfig.automationConfig.longitude == k_test_longitude);
    REQUIRE(runtimeConfig.automationConfig.latitude == k_test_latitude);
    REQUIRE(runtimeConfig.automationConfig.logIncomingMessages);
    REQUIRE(runtimeConfig.automationConfig.logOutgoingMessages);

    std::filesystem::remove(iniPath);
}
// NOLINTEND(readability-function-cognitive-complexity)

TEST_CASE("load_automation_client_runtime_config_reports_invalid_longitude", "[automation_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "port=1883\n"
        "clientId=automation-client\n"
        "\n"
        "[automation]\n"
        "longitude=not-a-number\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::AutomationClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadAutomationClientRuntimeConfigFromIni(
        document,
        runtimeConfig,
        errorMessage);

    REQUIRE_FALSE(success);
    REQUIRE(errorMessage == "invalid value for automation.longitude");

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_automation_client_runtime_config_reports_invalid_latitude", "[automation_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "port=1883\n"
        "clientId=automation-client\n"
        "\n"
        "[automation]\n"
        "latitude=not-a-number\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::AutomationClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadAutomationClientRuntimeConfigFromIni(
        document,
        runtimeConfig,
        errorMessage);

    REQUIRE_FALSE(success);
    REQUIRE(errorMessage == "invalid value for automation.latitude");

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_automation_client_runtime_config_defaults_logging_flags_to_false", "[automation_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "port=1883\n"
        "clientId=automation-client\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::AutomationClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadAutomationClientRuntimeConfigFromIni(
        document,
        runtimeConfig,
        errorMessage);

    REQUIRE(success);
    REQUIRE(errorMessage.empty());
    REQUIRE_FALSE(runtimeConfig.automationConfig.logIncomingMessages);
    REQUIRE_FALSE(runtimeConfig.automationConfig.logOutgoingMessages);

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_automation_client_runtime_config_reports_invalid_log_incoming_messages", "[automation_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "port=1883\n"
        "clientId=automation-client\n"
        "\n"
        "[automation]\n"
        "logIncomingMessages=maybe\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::AutomationClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadAutomationClientRuntimeConfigFromIni(
        document,
        runtimeConfig,
        errorMessage);

    REQUIRE_FALSE(success);
    REQUIRE(errorMessage.find("automation.logIncomingMessages") != std::string::npos);

    std::filesystem::remove(iniPath);
}

TEST_CASE("load_automation_client_runtime_config_reports_invalid_log_outgoing_messages", "[automation_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "host=127.0.0.1\n"
        "port=1883\n"
        "clientId=automation-client\n"
        "\n"
        "[automation]\n"
        "logOutgoingMessages=maybe\n";

    const auto iniPath = writeTempIni(iniText);
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniPath);

    yaha::AutomationClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};
    const bool success = yaha::tryLoadAutomationClientRuntimeConfigFromIni(
        document,
        runtimeConfig,
        errorMessage);

    REQUIRE_FALSE(success);
    REQUIRE(errorMessage.find("automation.logOutgoingMessages") != std::string::npos);

    std::filesystem::remove(iniPath);
}
