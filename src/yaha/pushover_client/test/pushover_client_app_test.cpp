#include "yaha/pushover_client/pushover_client_app.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
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

class ScopedPathPrefix {
public:
    explicit ScopedPathPrefix(const std::filesystem::path& prefixPath)
        : hadPreviousPath_(std::getenv("PATH") != nullptr)
        , previousPath_(hadPreviousPath_ ? std::getenv("PATH") : "") {
        const std::string newPath = hadPreviousPath_
            ? (prefixPath.string() + ":" + previousPath_)
            : prefixPath.string();
        setenv("PATH", newPath.c_str(), 1);
    }

    ~ScopedPathPrefix() {
        if (hadPreviousPath_) {
            setenv("PATH", previousPath_.c_str(), 1);
            return;
        }
        unsetenv("PATH");
    }

private:
    bool hadPreviousPath_{false};
    std::string previousPath_{};
};

[[nodiscard]] std::filesystem::path makeFakeCurlDirectory(const std::string& scriptBody) {
    const auto stamp = std::to_string(
        static_cast<long long>(std::filesystem::file_time_type::clock::now().time_since_epoch().count()));
    const auto directoryPath = std::filesystem::temp_directory_path() / ("pushover_curl_stub_" + stamp);
    std::filesystem::create_directories(directoryPath);

    const auto scriptPath = directoryPath / "curl";
    std::ofstream scriptFile{scriptPath};
    scriptFile << "#!/bin/sh\n";
    scriptFile << scriptBody;
    scriptFile << "\n";
    scriptFile.close();

    std::filesystem::permissions(
        scriptPath,
        std::filesystem::perms::owner_exec |
            std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);

    return directoryPath;
}

void removeDirectoryQuiet(const std::filesystem::path& directoryPath) {
    std::error_code errorCode{};
    std::filesystem::remove_all(directoryPath, errorCode);
}

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

TEST_CASE("load_config_rejects_subscription_qos_without_topic", "[pushover_client]") {
    const std::string iniText =
        "[pushover]\n"
        "token = token-abc\n"
        "user = user-def\n"
        "\n"
        "[device]\n"
        "name = mobile-1\n"
        "\n"
        "[subscription]\n"
        "qos = 1\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::PushoverConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryLoadPushoverConfigFromIni(document, config, errorMessage));
    REQUIRE(errorMessage.find("requires subscription.topic") != std::string::npos);
}

TEST_CASE("load_config_rejects_duplicate_subscription_qos", "[pushover_client]") {
    const std::string iniText =
        "[pushover]\n"
        "token = token-abc\n"
        "user = user-def\n"
        "\n"
        "[device]\n"
        "name = mobile-1\n"
        "\n"
        "[subscription]\n"
        "topic = home/alarm/#\n"
        "qos = 1\n"
        "qos = 0\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::PushoverConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryLoadPushoverConfigFromIni(document, config, errorMessage));
    REQUIRE(errorMessage.find("duplicate subscription.qos") != std::string::npos);
}

TEST_CASE("load_runtime_config_falls_back_on_invalid_mqtt_values", "[pushover_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "loopSleepMs=0\n"
        "\n"
        "[pushover]\n"
        "token = token-abc\n"
        "user = user-def\n"
        "\n"
        "[device]\n"
        "name = mobile-1\n"
        "\n"
        "[subscription]\n"
        "topic = home/alarm/#\n"
        "qos = 1\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::PushoverClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE(yaha::tryLoadPushoverClientRuntimeConfigFromIni(document, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeConfig.mqttConfig.loopSleep == std::chrono::milliseconds{20});
}

TEST_CASE("pushover_request_sender_parses_successful_curl_output", "[pushover_client]") {
    const auto fakeCurlDirectory = makeFakeCurlDirectory(
        "printf '{\"status\":1}\\n__YAHA_STATUS__:200\\n__YAHA_CTYPE__:application/json\\n'\n"
        "exit 0");
    const ScopedPathPrefix scopedPath{fakeCurlDirectory};

    const yaha::PushoverConfig config{
        .host = "example.org",
        .port = 443U,
        .path = "/1/messages.json",
    };
    const auto sender = yaha::makePushoverRequestSender(config);

    const yaha::PushoverHttpResult result = sender("ignored", R"({"message":"ok"})");
    REQUIRE(result.statusCode == 200);
    REQUIRE(result.payload.find("status") != std::string::npos);
    REQUIRE(result.contentType == "application/json");

    removeDirectoryQuiet(fakeCurlDirectory);
}

TEST_CASE("pushover_request_sender_throws_on_non_zero_curl_exit", "[pushover_client]") {
    const auto fakeCurlDirectory = makeFakeCurlDirectory("exit 2");
    const ScopedPathPrefix scopedPath{fakeCurlDirectory};

    const yaha::PushoverConfig config{
        .host = "example.org",
        .port = 443U,
        .path = "/1/messages.json",
    };
    const auto sender = yaha::makePushoverRequestSender(config);

    REQUIRE_THROWS(sender("ignored", "{\"message\":\"ok\"}"));

    removeDirectoryQuiet(fakeCurlDirectory);
}

TEST_CASE("pushover_request_sender_throws_on_missing_metadata", "[pushover_client]") {
    const auto fakeCurlDirectory = makeFakeCurlDirectory("printf 'body only'\nexit 0");
    const ScopedPathPrefix scopedPath{fakeCurlDirectory};

    const yaha::PushoverConfig config{
        .host = "example.org",
        .port = 443U,
        .path = "/1/messages.json",
    };
    const auto sender = yaha::makePushoverRequestSender(config);

    REQUIRE_THROWS(sender("ignored", "{\"message\":\"ok\"}"));

    removeDirectoryQuiet(fakeCurlDirectory);
}

TEST_CASE("load_config_rejects_invalid_device_shape", "[pushover_client]") {
    const std::string iniText =
        "[pushover]\n"
        "token = token-abc\n"
        "user = user-def\n"
        "\n"
        "[device]\n"
        "id = mobile-1\n"
        "\n"
        "[subscription]\n"
        "topic = home/alarm/#\n"
        "qos = 1\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::PushoverConfig config{};
    std::string errorMessage{};

    REQUIRE_FALSE(yaha::tryLoadPushoverConfigFromIni(document, config, errorMessage));
    REQUIRE(errorMessage.find("invalid key in [device]") != std::string::npos);
}

TEST_CASE("load_runtime_config_falls_back_on_invalid_pushover_port_and_mqtt", "[pushover_client]") {
    const std::string iniText =
        "[mqtt]\n"
        "port=invalid\n"
        "\n"
        "[pushover]\n"
        "token = token-abc\n"
        "user = user-def\n"
        "port = invalid\n"
        "\n"
        "[device]\n"
        "name = mobile-1\n"
        "\n"
        "[subscription]\n"
        "topic = home/alarm/#\n"
        "qos = 1\n";

    const ScopedIniFile iniFile{iniText};
    const yaha::IniDocument document = yaha::IniDocument::loadFromFile(iniFile.path());

    yaha::PushoverClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};

    REQUIRE(yaha::tryLoadPushoverClientRuntimeConfigFromIni(document, runtimeConfig, errorMessage));
    REQUIRE(errorMessage.empty());
    REQUIRE(runtimeConfig.pushoverConfig.port == 443U);
    REQUIRE(runtimeConfig.mqttConfig.brokerPort == yaha::YahaMqttClient::k_default_broker_port);
}

TEST_CASE("pushover_request_sender_throws_on_invalid_status_metadata", "[pushover_client]") {
    const auto fakeCurlDirectory = makeFakeCurlDirectory(
        "printf '{\"status\":1}\\n__YAHA_STATUS__:abc\\n__YAHA_CTYPE__:application/json\\n'\n"
        "exit 0");
    const ScopedPathPrefix scopedPath{fakeCurlDirectory};

    const yaha::PushoverConfig config{
        .host = "example.org",
        .port = 443U,
        .path = "/1/messages.json",
    };
    const auto sender = yaha::makePushoverRequestSender(config);

    REQUIRE_THROWS(sender("ignored", "{'message':'ok'}"));

    removeDirectoryQuiet(fakeCurlDirectory);
}
