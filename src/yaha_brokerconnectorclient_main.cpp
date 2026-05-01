#include "yaha/broker_connector/relay_component.h"
#include "yaha/broker_connector/source_http_adapter.h"
#include "yaha/broker_connector_client/broker_connector_client_app.h"
#include "yaha/mqtt_client/broker_transport.h"
#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"

#include <filesystem>
#include <exception>
#include <iostream>
#include <string>

namespace {

struct CliOptions {
    std::filesystem::path configPath{"broker.ini"};
    bool configPathProvided{false};
    bool showHelp{false};
};

void printUsage() {
    std::cout << "Usage: yahabrokerconnectorclient [config-path] [--help]\n"
              << "  config-path    optional INI config file (default: broker.ini)\n"
              << "  --help         print this help and exit\n"
              << std::flush;
}

bool tryParseCli(const int argc, char* argv[], CliOptions& options, std::string& errorText) {
    for (int argIndex = 1; argIndex < argc; ++argIndex) {
        const std::string argument{argv[argIndex]};
        if (argument == "--help" || argument == "-h") {
            options.showHelp = true;
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            errorText = "unknown argument: " + argument;
            return false;
        }

        if (options.configPathProvided) {
            errorText = "multiple config paths provided";
            return false;
        }

        options.configPath = std::filesystem::path{argument};
        options.configPathProvided = true;
    }

    return true;
}

void printStartupConfiguration(const std::filesystem::path& configPath,
                               const yaha::BrokerConnectorClientRuntimeConfig& runtimeConfig) {
    std::cout << "yahabrokerconnectorclient\n";
    std::cout << "  config: " << configPath.string() << '\n';
    std::cout << "  source: " << runtimeConfig.sourceConfig.brokerHost << ':'
              << runtimeConfig.sourceConfig.brokerPort
              << " clientId=" << runtimeConfig.sourceConfig.clientId << '\n';
    std::cout << "  source-listener: " << runtimeConfig.sourceConfig.listenerHost << ':'
              << runtimeConfig.sourceConfig.listenerPort << '\n';
    std::cout << "  receiver: " << runtimeConfig.receiverConfig.brokerHost << ':'
              << runtimeConfig.receiverConfig.brokerPort
              << " clientId=" << runtimeConfig.receiverConfig.clientId << '\n';
    std::cout << "  relay: retries=" << runtimeConfig.relayPolicyConfig.maxPublishRetries
              << " backoffMs=" << runtimeConfig.relayPolicyConfig.publishRetryBackoff.count()
              << " normalizeQos=" << (runtimeConfig.relayPolicyConfig.normalizeQosToAtLeastOnce ? "1" : "0")
              << " retainPassthrough=" << (runtimeConfig.relayPolicyConfig.retainPassthrough ? "1" : "0")
              << '\n';
    std::cout << std::flush;
}

} // namespace

int main(int argc, char* argv[]) {
    CliOptions cliOptions{};
    std::string cliError{};
    if (!tryParseCli(argc, argv, cliOptions, cliError)) {
        std::cerr << "Failed to parse arguments: " << cliError << '\n';
        printUsage();
        return 1;
    }

    if (cliOptions.showHelp) {
        printUsage();
        return 0;
    }

    yaha::IniDocument configDocument{};
    try {
        configDocument = yaha::IniDocument::loadFromFile(cliOptions.configPath);
    } catch (const std::exception& exceptionValue) {
        std::cerr << "Failed to load config file '" << cliOptions.configPath.string()
                  << "': " << exceptionValue.what() << '\n';
        return 1;
    }

    const auto runtimeConfigResult = yaha::tryLoadBrokerConnectorClientRuntimeConfigFromIni(configDocument);
    if (!runtimeConfigResult.config.has_value()) {
        std::cerr << "Failed to parse connector config from '" << cliOptions.configPath.string()
                  << "': " << runtimeConfigResult.errorMessage << '\n';
        return 1;
    }
    const yaha::BrokerConnectorClientRuntimeConfig runtimeConfig = *runtimeConfigResult.config;

    printStartupConfiguration(cliOptions.configPath, runtimeConfig);

    yaha::SourceHttpBrokerAdapter sourceAdapter{runtimeConfig.sourceConfig};
    yaha::BrokerConnectorComponent relayComponent{runtimeConfig.relayPolicyConfig};
    relayComponent.setSourceAdapter(sourceAdapter, runtimeConfig.sourceLifecycleConfig);

    yaha::YahaMqttClient receiverClient{
        runtimeConfig.receiverConfig,
        relayComponent,
        yaha::makeBrokerTransport()};
    yaha::YahaMqttClientRuntime runtime{receiverClient, relayComponent};
    runtime.runUntilSignal();

    return 0;
}
