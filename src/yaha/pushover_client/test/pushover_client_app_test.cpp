#include "yaha/pushover_client/pushover_client_app.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

class ScopedIniFile {
public:
    explicit ScopedIniFile(const std::string& content)
        : path_(std::filesystem::temp_directory_path() / "pushover_client_app_test.ini") {
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

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("load_runtime_config_parses_pushover_devices_and_subscriptions", "[pushover_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "host = 127.0.0.1\n"
        "port = 1883\n"
        "clientId = yahapushoverclient\n"
        "\n"
        "[pushover]\n"
        "host = api.pushover.net\n"
        "path = /1/messages.json\n"
        "port = 443\n"
        "token = token-abc\n"
        "user = user-def\n"
        "\n"
        "[device]\n"
        "name = mobile-1\n"
        "\n"
        "[device]\n"
        "name = mobile-2\n"
        "\n"
        "[subscription]\n"
        "topic = $SYS/incident/#\n"
        "qos = 1\n"
        "\n"
        "[subscription]\n"
        "topic = home/alarm/#\n"
        "qos = 0\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::PushoverClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE(yaha::tryLoadPushoverClientRuntimeConfigFromIni(document, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeConfig.pushoverConfig.host == "api.pushover.net");
    REQUIRE(runtimeConfig.pushoverConfig.path == "/1/messages.json");
    REQUIRE(runtimeConfig.pushoverConfig.devices.size() == 2U);
    REQUIRE(runtimeConfig.pushoverConfig.subscriptions.size() == 2U);
    REQUIRE(runtimeConfig.pushoverConfig.subscriptions[1].topicFilter == "home/alarm/#");
    REQUIRE(runtimeConfig.pushoverConfig.subscriptions[1].qos == yaha::Qos::AtMostOnce);
}

TEST_CASE("load_config_requires_device_entries", "[pushover_client]") {
    const std::string iniText =
        "[pushover]\n"
        "token = token-abc\n"
        "user = user-def\n"
        "\n"
        "[subscription]\n"
        "topic = $SYS/incident/#\n"
        "qos = 1\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::PushoverConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryLoadPushoverConfigFromIni(document, config, errorMessage));
    REQUIRE(errorMessage.find("missing [device]") != std::string::npos);
}

TEST_CASE("load_config_requires_subscription_entries", "[pushover_client]") {
    const std::string iniText =
        "[pushover]\n"
        "token = token-abc\n"
        "user = user-def\n"
        "\n"
        "[device]\n"
        "name = mobile-1\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::PushoverConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryLoadPushoverConfigFromIni(document, config, errorMessage));
    REQUIRE(errorMessage.find("missing [subscription]") != std::string::npos);
}

TEST_CASE("load_config_requires_token_and_user", "[pushover_client]") {
    const std::string iniText =
        "[pushover]\n"
        "host = api.pushover.net\n"
        "\n"
        "[device]\n"
        "name = mobile-1\n"
        "\n"
        "[subscription]\n"
        "topic = $SYS/incident/#\n"
        "qos = 1\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::PushoverConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryLoadPushoverConfigFromIni(document, config, errorMessage));
    REQUIRE(errorMessage == "pushover.token must not be empty");
}
