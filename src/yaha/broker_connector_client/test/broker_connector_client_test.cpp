#include <catch2/catch_test_macros.hpp>

#include "yaha/broker_connector_client/broker_connector_client_app.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr std::uint16_t k_expected_source_port{8080U};
constexpr std::uint16_t k_expected_source_listener_port{18080U};
constexpr std::uint32_t k_expected_source_keep_alive_seconds{11U};
constexpr std::uint16_t k_expected_receiver_port{1884U};
constexpr std::int64_t k_expected_receiver_reconnect_delay_ms{222};
constexpr std::int64_t k_expected_receiver_keep_alive_interval_ms{12000};
constexpr std::int64_t k_expected_receiver_loop_sleep_ms{7};
constexpr std::int64_t k_expected_source_lifecycle_reconnect_delay_ms{333};
constexpr std::int64_t k_expected_source_lifecycle_loop_sleep_ms{8};
constexpr std::int64_t k_expected_source_lifecycle_keep_alive_interval_ms{444};
constexpr std::uint32_t k_expected_max_publish_retries{5U};
constexpr std::int64_t k_expected_publish_retry_backoff_ms{9};

std::filesystem::path make_temp_directory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_broker_connector_client_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void remove_directory_quiet(const std::filesystem::path& path) {
    std::error_code error_code{};
    std::filesystem::remove_all(path, error_code);
}

std::filesystem::path write_config_file(const std::filesystem::path& directory,
                                        const std::string& content) {
    const auto path = directory / "connector.ini";
    std::ofstream output{path};
    output << content;
    return path;
}

yaha::BrokerConnectorClientRuntimeConfigLoadResult try_load_runtime_config_from_file(
    const std::filesystem::path& config_path) {
    yaha::IniDocument document{};
    try {
        document = yaha::IniDocument::loadFromFile(config_path);
    } catch (const std::exception& exception) {
        return {.config = std::nullopt, .errorMessage = exception.what()};
    }

    return yaha::tryLoadBrokerConnectorClientRuntimeConfigFromIni(document);
}

bool source_config_matches_complete_ini(
    const yaha::BrokerConnectorClientRuntimeConfig& config) {
    return config.sourceConfig.brokerHost == "source.local" &&
        config.sourceConfig.brokerPort == k_expected_source_port &&
        config.sourceConfig.clientId == "source-client" &&
        config.sourceConfig.listenerHost == "127.0.0.1" &&
        config.sourceConfig.listenerBindHost == "0.0.0.0" &&
        config.sourceConfig.listenerPort == k_expected_source_listener_port &&
        config.sourceConfig.keepAliveSeconds == k_expected_source_keep_alive_seconds &&
        !config.sourceConfig.clean &&
        config.sourceConfig.subscribeTopics.size() == 1U;
}

bool receiver_config_matches_complete_ini(
    const yaha::BrokerConnectorClientRuntimeConfig& config) {
    return config.receiverConfig.brokerHost == "receiver.local" &&
        config.receiverConfig.brokerPort == k_expected_receiver_port &&
        config.receiverConfig.clientId == "receiver-client" &&
        config.receiverConfig.reconnectDelay.count() == k_expected_receiver_reconnect_delay_ms &&
        config.receiverConfig.keepAliveInterval.count() == k_expected_receiver_keep_alive_interval_ms &&
        config.receiverConfig.loopSleep.count() == k_expected_receiver_loop_sleep_ms &&
        !config.receiverConfig.enableLifecycleTrace &&
        config.receiverConfig.enableMessageTrace;
}

bool lifecycle_and_policy_match_complete_ini(
    const yaha::BrokerConnectorClientRuntimeConfig& config) {
    return config.sourceLifecycleConfig.reconnectDelay.count() == k_expected_source_lifecycle_reconnect_delay_ms &&
        config.sourceLifecycleConfig.loopSleep.count() == k_expected_source_lifecycle_loop_sleep_ms &&
        config.sourceLifecycleConfig.keepAliveInterval.count() == k_expected_source_lifecycle_keep_alive_interval_ms &&
        config.relayPolicyConfig.maxPublishRetries == k_expected_max_publish_retries &&
        config.relayPolicyConfig.publishRetryBackoff.count() == k_expected_publish_retry_backoff_ms &&
        !config.relayPolicyConfig.normalizeQosToAtLeastOnce &&
        config.relayPolicyConfig.retainPassthrough;
}

} // namespace

TEST_CASE("broker_connector_client_config_parses_complete_ini",
          "[broker_connector_client]") {
    const auto temp_directory = make_temp_directory();

    const auto config_path = write_config_file(temp_directory,
        "[sourceHttpBroker]\n"
        "host = source.local\n"
        "port = 8080\n"
        "clientId = source-client\n"
        "listenerHost = 127.0.0.1\n"
        "listenerBindHost = 0.0.0.0\n"
        "listenerPort = 18080\n"
        "keepAliveSeconds = 11\n"
        "clean = no\n"
        "\n"
        "[subscription]\n"
        "topic = home/#\n"
        "qos = 1\n"
        "\n"
        "[receiverMqttBroker]\n"
        "host = receiver.local\n"
        "port = 1884\n"
        "clientId = receiver-client\n"
        "reconnectDelayMs = 222\n"
        "keepAliveSeconds = 12\n"
        "loopSleepMs = 7\n"
        "enableLifecycleTrace = off\n"
        "\n"
        "[automation]\n"
        "reconnectDelayMs = 333\n"
        "sourceLoopSleepMs = 8\n"
        "sourceKeepAliveIntervalMs = 444\n"
        "maxPublishRetries = 5\n"
        "publishRetryBackoffMs = 9\n"
        "normalizeQosToAtLeastOnce = false\n"
        "retainPassthrough = true\n"
        "\n"
        "[monitoring]\n"
        "logOutgoingMessage = true\n");

    const auto runtime_config_result = try_load_runtime_config_from_file(config_path);
    REQUIRE(runtime_config_result.config.has_value());
    const auto config = *runtime_config_result.config;
    REQUIRE(source_config_matches_complete_ini(config));
    REQUIRE(receiver_config_matches_complete_ini(config));
    REQUIRE(lifecycle_and_policy_match_complete_ini(config));

    remove_directory_quiet(temp_directory);
}

TEST_CASE("broker_connector_client_config_applies_keepalive_fallback_and_monitoring_trace",
          "[broker_connector_client]") {
    const auto temp_directory = make_temp_directory();

    const auto config_path = write_config_file(temp_directory,
        "[sourceHttpBroker]\n"
        "host = source.local\n"
        "keepAliveSeconds = 13\n"
        "\n"
        "[receiverMqttBroker]\n"
        "host = receiver.local\n"
        "\n"
        "[automation]\n"
        "reconnectDelayMs = 100\n"
        "sourceLoopSleepMs = 10\n"
        "maxPublishRetries = 1\n"
        "publishRetryBackoffMs = 5\n"
        "normalizeQosToAtLeastOnce = true\n"
        "retainPassthrough = false\n"
        "\n"
        "[monitoring]\n"
        "sourceLifecycleTrace = true\n");

    const auto runtime_config_result = try_load_runtime_config_from_file(config_path);
    REQUIRE(runtime_config_result.config.has_value());
    const auto config = *runtime_config_result.config;
    REQUIRE(config.sourceLifecycleConfig.keepAliveInterval.count() == 13000);
    REQUIRE(config.sourceLifecycleConfig.enableTrace);

    remove_directory_quiet(temp_directory);
}

TEST_CASE("broker_connector_client_config_rejects_invalid_boolean",
          "[broker_connector_client]") {
    const auto temp_directory = make_temp_directory();
    const auto config_path = write_config_file(temp_directory,
        "[monitoring]\n"
        "logOutgoingMessage = maybe\n");

    const auto runtime_config_result = try_load_runtime_config_from_file(config_path);
    REQUIRE_FALSE(runtime_config_result.config.has_value());
    REQUIRE(runtime_config_result.errorMessage.find("monitoring.logOutgoingMessage") != std::string::npos);

    remove_directory_quiet(temp_directory);
}

TEST_CASE("broker_connector_client_config_rejects_invalid_source_port",
          "[broker_connector_client]") {
    const auto temp_directory = make_temp_directory();
    const auto config_path = write_config_file(temp_directory,
        "[sourceHttpBroker]\n"
        "host = source.local\n"
        "port = 70000\n"
        "\n"
        "[receiverMqttBroker]\n"
        "host = receiver.local\n");

    const auto runtime_config_result = try_load_runtime_config_from_file(config_path);
    REQUIRE_FALSE(runtime_config_result.config.has_value());
    REQUIRE(runtime_config_result.errorMessage.find("sourceHttpBroker.port") != std::string::npos);

    remove_directory_quiet(temp_directory);
}

TEST_CASE("broker_connector_client_config_rejects_invalid_monitoring_trace_boolean",
          "[broker_connector_client]") {
    const auto temp_directory = make_temp_directory();
    const auto config_path = write_config_file(temp_directory,
        "[sourceHttpBroker]\n"
        "host = source.local\n"
        "\n"
        "[receiverMqttBroker]\n"
        "host = receiver.local\n"
        "\n"
        "[monitoring]\n"
        "sourceLifecycleTrace = maybe\n");

    const auto runtime_config_result = try_load_runtime_config_from_file(config_path);
    REQUIRE_FALSE(runtime_config_result.config.has_value());
    REQUIRE(runtime_config_result.errorMessage.find("monitoring.sourceLifecycleTrace") != std::string::npos);

    remove_directory_quiet(temp_directory);
}

TEST_CASE("broker_connector_client_config_rejects_invalid_source_optional_fields",
          "[broker_connector_client]") {
    const auto temp_directory = make_temp_directory();

    {
        const auto config_path = write_config_file(temp_directory,
            "[sourceHttpBroker]\n"
            "host = source.local\n"
            "listenerPort = 70000\n"
            "\n"
            "[receiverMqttBroker]\n"
            "host = receiver.local\n");
        const auto runtime_config_result = try_load_runtime_config_from_file(config_path);
        REQUIRE_FALSE(runtime_config_result.config.has_value());
        REQUIRE(runtime_config_result.errorMessage.find("sourceHttpBroker.listenerPort") != std::string::npos);
    }

    {
        const auto config_path = write_config_file(temp_directory,
            "[sourceHttpBroker]\n"
            "host = source.local\n"
            "keepAliveSeconds = 0\n"
            "\n"
            "[receiverMqttBroker]\n"
            "host = receiver.local\n");
        const auto runtime_config_result = try_load_runtime_config_from_file(config_path);
        REQUIRE_FALSE(runtime_config_result.config.has_value());
        REQUIRE(runtime_config_result.errorMessage.find("sourceHttpBroker.keepAliveSeconds") != std::string::npos);
    }

    {
        const auto config_path = write_config_file(temp_directory,
            "[sourceHttpBroker]\n"
            "host = source.local\n"
            "clean = maybe\n"
            "\n"
            "[receiverMqttBroker]\n"
            "host = receiver.local\n");
        const auto runtime_config_result = try_load_runtime_config_from_file(config_path);
        REQUIRE_FALSE(runtime_config_result.config.has_value());
        REQUIRE(runtime_config_result.errorMessage.find("sourceHttpBroker.clean") != std::string::npos);
    }

    remove_directory_quiet(temp_directory);
}

TEST_CASE("broker_connector_client_config_rejects_invalid_receiver_and_automation_fields",
          "[broker_connector_client]") {
    const auto temp_directory = make_temp_directory();

    {
        const auto config_path = write_config_file(temp_directory,
            "[sourceHttpBroker]\n"
            "host = source.local\n"
            "\n"
            "[receiverMqttBroker]\n"
            "host = receiver.local\n"
            "port = 70000\n");
        const auto runtime_config_result = try_load_runtime_config_from_file(config_path);
        REQUIRE_FALSE(runtime_config_result.config.has_value());
        REQUIRE(runtime_config_result.errorMessage.find("receiverMqttBroker.port") != std::string::npos);
    }

    {
        const auto config_path = write_config_file(temp_directory,
            "[sourceHttpBroker]\n"
            "host = source.local\n"
            "\n"
            "[receiverMqttBroker]\n"
            "host = receiver.local\n"
            "\n"
            "[automation]\n"
            "reconnectDelayMs = 0\n");
        const auto runtime_config_result = try_load_runtime_config_from_file(config_path);
        REQUIRE_FALSE(runtime_config_result.config.has_value());
        REQUIRE(runtime_config_result.errorMessage.find("automation.reconnectDelayMs") != std::string::npos);
    }

    {
        const auto config_path = write_config_file(temp_directory,
            "[sourceHttpBroker]\n"
            "host = source.local\n"
            "\n"
            "[receiverMqttBroker]\n"
            "host = receiver.local\n"
            "\n"
            "[automation]\n"
            "normalizeQosToAtLeastOnce = maybe\n");
        const auto runtime_config_result = try_load_runtime_config_from_file(config_path);
        REQUIRE_FALSE(runtime_config_result.config.has_value());
        REQUIRE(runtime_config_result.errorMessage.find("automation.normalizeQosToAtLeastOnce") != std::string::npos);
    }

    remove_directory_quiet(temp_directory);
}
