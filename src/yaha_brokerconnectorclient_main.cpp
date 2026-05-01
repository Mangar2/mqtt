#include "yaha/broker_connector/receiver_publish_port.h"
#include "yaha/broker_connector/relay_component.h"
#include "yaha/broker_connector/source_http_adapter.h"
#include "yaha/broker_connector/source_lifecycle_manager.h"
#include "yaha/broker_connector_client/broker_connector_client_app.h"
#include "yaha/broker_connector_client/broker_connector_runtime.h"

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

class SourceLifecycleRuntimeAdapter final : public yaha::SourceRuntimePort {
public:
    explicit SourceLifecycleRuntimeAdapter(yaha::SourceLifecycleManager& manager)
        : manager_(manager) {}

    void run() override {
        manager_.run();
    }

    void close() override {
        manager_.close();
    }

private:
    yaha::SourceLifecycleManager& manager_;
};

class RelayComponentRuntimeAdapter final : public yaha::ConnectorRuntimePort {
public:
    explicit RelayComponentRuntimeAdapter(yaha::BrokerConnectorComponent& component)
        : component_(component) {}

    void run() override {
        component_.run();
    }

    void close() override {
        component_.close();
    }

private:
    yaha::BrokerConnectorComponent& component_;
};

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
    std::string errorMessage{};
    try {
        configDocument = yaha::IniDocument::loadFromFile(cliOptions.configPath);
    } catch (const std::exception& exceptionValue) {
        std::cerr << "Failed to load config file '" << cliOptions.configPath.string()
                  << "': " << exceptionValue.what() << '\n';
        return 1;
    }

    yaha::BrokerConnectorClientRuntimeConfig runtimeConfig{};
    if (!yaha::tryLoadBrokerConnectorClientRuntimeConfigFromIni(
            configDocument,
            runtimeConfig,
            errorMessage)) {
        std::cerr << "Failed to parse connector config from '" << cliOptions.configPath.string()
                  << "': " << errorMessage << '\n';
        return 1;
    }

    printStartupConfiguration(cliOptions.configPath, runtimeConfig);

    yaha::ReceiverMqttPublishPort receiverPort{runtimeConfig.receiverConfig};
    yaha::BrokerConnectorComponent relayComponent{runtimeConfig.relayPolicyConfig};
    relayComponent.setReceiverPublishPort(receiverPort);

    yaha::SourceHttpBrokerAdapter sourceAdapter{runtimeConfig.sourceConfig};
    sourceAdapter.setIncomingPublishCallback(
        [&relayComponent](const yaha::Message& message, const yaha::SourcePublishMeta& sourceMeta) {
            (void)relayComponent.onIncomingPublish(message, sourceMeta);
        });

    yaha::SourceLifecycleManager sourceLifecycle{sourceAdapter, runtimeConfig.sourceLifecycleConfig};
    SourceLifecycleRuntimeAdapter sourceRuntime{sourceLifecycle};
    RelayComponentRuntimeAdapter connectorRuntime{relayComponent};

    yaha::BrokerConnectorClientRuntime runtime{receiverPort, sourceRuntime, connectorRuntime};

    if (!runtime.runUntilSignal(errorMessage)) {
        std::cerr << "Runtime startup failed: " << errorMessage << '\n';
        return 1;
    }

    return 0;
}
