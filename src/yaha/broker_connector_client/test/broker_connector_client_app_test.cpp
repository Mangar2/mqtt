#include <catch2/catch_test_macros.hpp>

#include "yaha/broker_connector_client/broker_connector_client_app.h"

#include <filesystem>
#include <fstream>
#include <exception>
#include <string>

namespace {

bool loadDocumentFromText(const std::string& text,
                          yaha::IniDocument& document,
                          std::string& errorMessage) {
    std::filesystem::path filePath = std::filesystem::temp_directory_path() / "broker_connector_client_test.ini";
    std::ofstream output{filePath};
    output << text;
    output.close();

    auto result = true;
    try {
        document = yaha::IniDocument::loadFromFile(filePath);
    } catch (const std::exception& exceptionValue) {
        errorMessage = exceptionValue.what();
        result = false;
    }
    std::error_code removeError{};
    std::filesystem::remove(filePath, removeError);
    return result;
}

} // namespace

TEST_CASE("load_runtime_config_parses_source_receiver_and_automation", "[broker_connector_client]") {
    const std::string iniText =
        "[sourceHttpBroker]\n"
        "host=source.local\n"
        "port=8081\n"
        "clientId=src-client\n"
        "clean=false\n"
        "keepAliveSeconds=45\n"
        "listenerHost=0.0.0.0\n"
        "listenerPort=18080\n"
        "\n"
        "[subscription]\n"
        "topic=home/#\n"
        "qos=1\n"
        "\n"
        "[subscription]\n"
        "topic=sensor/+/state\n"
        "qos=0\n"
        "\n"
        "[receiverMqttBroker]\n"
        "host=receiver.local\n"
        "port=1884\n"
        "clientId=rcv-client\n"
        "reconnectDelayMs=2500\n"
        "keepAliveSeconds=60\n"
        "loopSleepMs=25\n"
        "enableLifecycleTrace=false\n"
        "enableMessageTrace=true\n"
        "\n"
        "[automation]\n"
        "reconnectDelayMs=3000\n"
        "sourceLoopSleepMs=30\n"
        "sourceKeepAliveIntervalMs=5000\n"
        "maxPublishRetries=5\n"
        "publishRetryBackoffMs=350\n"
        "normalizeQosToAtLeastOnce=false\n"
        "retainPassthrough=false\n"
        "\n"
        "[monitoring]\n"
        "sourceLifecycleTrace=false\n";

    yaha::IniDocument document{};
    std::string errorMessage{};
    REQUIRE(loadDocumentFromText(iniText, document, errorMessage));

    const auto runtimeConfigResult = yaha::tryLoadBrokerConnectorClientRuntimeConfigFromIni(document);
    REQUIRE(runtimeConfigResult.config.has_value());
    const yaha::BrokerConnectorClientRuntimeConfig config = *runtimeConfigResult.config;

    REQUIRE(config.sourceConfig.brokerHost == "source.local");
    REQUIRE(config.sourceConfig.brokerPort == 8081U);
    REQUIRE(config.sourceConfig.clientId == "src-client");
    REQUIRE_FALSE(config.sourceConfig.clean);
    REQUIRE(config.sourceConfig.keepAliveSeconds == 45U);
    REQUIRE(config.sourceConfig.listenerHost == "0.0.0.0");
    REQUIRE(config.sourceConfig.listenerPort == 18080U);
    REQUIRE(config.sourceConfig.subscribeTopics.size() == 2U);

    REQUIRE(config.receiverConfig.brokerHost == "receiver.local");
    REQUIRE(config.receiverConfig.brokerPort == 1884U);
    REQUIRE(config.receiverConfig.clientId == "rcv-client");
    REQUIRE(config.receiverConfig.reconnectDelay.count() == 2500);
    REQUIRE(config.receiverConfig.keepAliveInterval.count() == 60000);
    REQUIRE(config.receiverConfig.loopSleep.count() == 25);
    REQUIRE_FALSE(config.receiverConfig.enableLifecycleTrace);
    REQUIRE(config.receiverConfig.enableMessageTrace);

    REQUIRE(config.sourceLifecycleConfig.reconnectDelay.count() == 3000);
    REQUIRE(config.sourceLifecycleConfig.loopSleep.count() == 30);
    REQUIRE(config.sourceLifecycleConfig.keepAliveInterval.count() == 5000);
    REQUIRE_FALSE(config.sourceLifecycleConfig.enableTrace);

    REQUIRE(config.relayPolicyConfig.maxPublishRetries == 5U);
    REQUIRE(config.relayPolicyConfig.publishRetryBackoff.count() == 350);
    REQUIRE_FALSE(config.relayPolicyConfig.normalizeQosToAtLeastOnce);
    REQUIRE_FALSE(config.relayPolicyConfig.retainPassthrough);
}

TEST_CASE("load_runtime_config_uses_defaults_when_optional_keys_missing", "[broker_connector_client]") {
    const std::string iniText =
        "[sourceHttpBroker]\n"
        "host=127.0.0.1\n"
        "port=8080\n"
        "\n"
        "[receiverMqttBroker]\n"
        "host=127.0.0.1\n"
        "port=1883\n";

    yaha::IniDocument document{};
    std::string errorMessage{};
    REQUIRE(loadDocumentFromText(iniText, document, errorMessage));

    const auto runtimeConfigResult = yaha::tryLoadBrokerConnectorClientRuntimeConfigFromIni(document);
    REQUIRE(runtimeConfigResult.config.has_value());
    const yaha::BrokerConnectorClientRuntimeConfig config = *runtimeConfigResult.config;

    REQUIRE(config.sourceConfig.clientId == "broker-connector-source");
    REQUIRE(config.sourceConfig.clean);
    REQUIRE(config.sourceConfig.subscribeTopics.size() == 1U);
    REQUIRE(config.sourceConfig.subscribeTopics.count("#") == 1U);

    REQUIRE(config.receiverConfig.clientId == "broker-connector-receiver");
    REQUIRE(config.relayPolicyConfig.maxPublishRetries == 3U);
    REQUIRE(config.relayPolicyConfig.normalizeQosToAtLeastOnce);
}

TEST_CASE("load_runtime_config_rejects_invalid_bool_field", "[broker_connector_client]") {
    const std::string iniText =
        "[sourceHttpBroker]\n"
        "host=127.0.0.1\n"
        "port=8080\n"
        "\n"
        "[receiverMqttBroker]\n"
        "host=127.0.0.1\n"
        "port=1883\n"
        "\n"
        "[automation]\n"
        "retainPassthrough=maybe\n";

    yaha::IniDocument document{};
    std::string errorMessage{};
    REQUIRE(loadDocumentFromText(iniText, document, errorMessage));

    const auto runtimeConfigResult = yaha::tryLoadBrokerConnectorClientRuntimeConfigFromIni(document);
    REQUIRE_FALSE(runtimeConfigResult.config.has_value());
    REQUIRE(runtimeConfigResult.errorMessage.find("automation.retainPassthrough") != std::string::npos);
}

TEST_CASE("load_runtime_config_rejects_incomplete_subscription_entry", "[broker_connector_client]") {
    const std::string iniText =
        "[sourceHttpBroker]\n"
        "host=127.0.0.1\n"
        "port=8080\n"
        "\n"
        "[subscription]\n"
        "topic=home/#\n"
        "\n"
        "[receiverMqttBroker]\n"
        "host=127.0.0.1\n"
        "port=1883\n";

    yaha::IniDocument document{};
    std::string errorMessage{};
    REQUIRE(loadDocumentFromText(iniText, document, errorMessage));

    const auto runtimeConfigResult = yaha::tryLoadBrokerConnectorClientRuntimeConfigFromIni(document);
    REQUIRE_FALSE(runtimeConfigResult.config.has_value());
    REQUIRE(runtimeConfigResult.errorMessage.find("incomplete [subscription] entry") != std::string::npos);
}
