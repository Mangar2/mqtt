#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "yaha/automation_client/automation_client_app.h"

namespace {

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
        "managementTopicPrefix=$MONITORING/automation/rules\n"
        "subscribeQoS=1\n";

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
    REQUIRE(runtimeConfig.automationConfig.managementTopicPrefix == "$MONITORING/automation/rules");

    std::filesystem::remove(iniPath);
}
// NOLINTEND(readability-function-cognitive-complexity)
