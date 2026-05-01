#include "yaha/message_store_client/message_store_client_app.h"
#include "yaha/mqtt_client/broker_transport.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct CliOptions {
    std::filesystem::path configPath{"broker.ini"};
    bool configPathProvided{false};
    bool enableMessageTrace{false};
    bool showHelp{false};
};

void printUsage() {
    std::cout << "Usage: yahamsgstoreclient [config-path] [--trace-messages] [--help]\n"
              << "  config-path         optional INI config file (default: broker.ini)\n"
              << "  --trace-messages    print sent/received MQTT messages\n"
              << "  --help              print this help and exit\n"
              << std::flush;
}

bool tryParseCli(int argc, char* argv[], CliOptions& options, std::string& errorText) {
    for (int argIndex = 1; argIndex < argc; ++argIndex) {
        const std::string argument{argv[argIndex]};
        if (argument == "--help" || argument == "-h") {
            options.showHelp = true;
            continue;
        }

        if (argument == "--trace-messages") {
            options.enableMessageTrace = true;
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

const char* qosToText(const yaha::Qos qos) {
    switch (qos) {
        case yaha::Qos::AtMostOnce:
            return "0";
        case yaha::Qos::AtLeastOnce:
            return "1";
        case yaha::Qos::ExactlyOnce:
            return "2";
    }

    return "0";
}

void printStartupConfiguration(const std::filesystem::path& configPath,
                               const yaha::MessageStoreClientRuntimeConfig& runtimeConfig) {
    std::cout << "yahamsgstoreclient\n";
    std::cout << "  config: " << configPath.string() << '\n';
    std::cout << "  mqtt: " << runtimeConfig.mqttConfig.brokerHost << ':'
              << runtimeConfig.mqttConfig.brokerPort
              << " clientId=" << runtimeConfig.mqttConfig.clientId << '\n';
    std::cout << "  http: " << runtimeConfig.storeConfig.serverHost << ':'
              << runtimeConfig.storeConfig.serverPort << runtimeConfig.storeConfig.serverPath << '\n';
    std::cout << "  persist: " << runtimeConfig.storeConfig.persistenceConfig.directory << '/'
              << runtimeConfig.storeConfig.persistenceConfig.filename
              << " intervalMs=" << runtimeConfig.storeConfig.persistenceConfig.intervalMs
              << " keepFiles=" << runtimeConfig.storeConfig.persistenceConfig.keepFiles << '\n';
    std::cout << "  subscriptions:";
    for (const auto& subscription : runtimeConfig.storeConfig.subscriptions) {
        std::cout << " [" << subscription.first << "=>qos" << qosToText(subscription.second) << ']';
    }
    std::cout << '\n';
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

    const std::filesystem::path configPath = cliOptions.configPath;

    yaha::IniDocument configDocument{};
    std::string errorMessage{};
    try {
        configDocument = yaha::IniDocument::loadFromFile(configPath);
    } catch (const std::exception& exceptionValue) {
        std::cerr << "Failed to load config file '" << configPath.string()
                  << "': " << exceptionValue.what() << '\n';
        return 1;
    }

    yaha::MessageStoreClientRuntimeConfig runtimeConfig{};
    if (!yaha::tryLoadMessageStoreClientRuntimeConfigFromIni(
            configDocument,
            runtimeConfig,
            errorMessage)) {
        std::cerr << "Failed to load MessageStore config from '" << configPath.string()
                  << "': " << errorMessage << '\n';
        return 1;
    }

    runtimeConfig.mqttConfig.enableMessageTrace = cliOptions.enableMessageTrace;

    const std::string configuredHttpHost = runtimeConfig.storeConfig.serverHost;
    const std::string configuredHttpPath = runtimeConfig.storeConfig.serverPath;
    const std::uint16_t configuredHttpPort = runtimeConfig.storeConfig.serverPort;

    printStartupConfiguration(configPath, runtimeConfig);

    yaha::MessageStore store{std::move(runtimeConfig.storeConfig)};
    yaha::YahaMqttClient mqttClient{
        std::move(runtimeConfig.mqttConfig),
        store,
        yaha::makeBrokerTransport()};

    std::cout << "  runtime: started\n";
    if (configuredHttpPort == 0U) {
        std::cout << "  http: disabled (server.port=0)\n";
    } else {
        const std::string effectiveHost =
            configuredHttpHost.empty() ? "127.0.0.1" : configuredHttpHost;
        std::cout << "  http: listening on http://" << effectiveHost << ':' << configuredHttpPort
                  << configuredHttpPath << "\n";
    }
    std::cout << "  signal: waiting for SIGINT/SIGTERM\n";
    std::cout << std::flush;

    yaha::YahaMqttClientRuntime runtime{mqttClient, store};
    runtime.runUntilSignal();

    std::cout << "  signal: received, disconnecting\n";
    std::cout << "  runtime: shutting down\n";
    std::cout << "  runtime: stopped\n";
    std::cout << std::flush;
    return 0;
}
