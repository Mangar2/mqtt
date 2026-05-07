#include "yaha/mqtt_client/broker_transport.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"
#include "yaha/value_service/value_service_component.h"
#include "yaha/value_service_client/value_service_client_app.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace {

struct CliOptions {
    std::filesystem::path configPath{"broker.ini"};
    bool configPathProvided{false};
    bool enableMessageTrace{false};
    bool showHelp{false};
};

void printUsage() {
    std::cout << "Usage: yahavalueserviceclient [config-path] [--trace-messages] [--help]\n"
              << "  config-path         optional INI config file (default: broker.ini)\n"
              << "  --trace-messages    print sent/received MQTT messages\n"
              << "  --help              print this help and exit\n"
              << std::flush;
}

bool tryParseCli(const int argc, char* argv[], CliOptions& options, std::string& errorText) {
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

void printStartupConfiguration(const std::filesystem::path& configPath,
                               const yaha::ValueServiceClientRuntimeConfig& runtimeConfig) {
    std::cout << "yahavalueserviceclient\n";
    std::cout << "  config: " << configPath.string() << '\n';
    std::cout << "  mqtt: " << runtimeConfig.mqttConfig.brokerHost << ':'
              << runtimeConfig.mqttConfig.brokerPort
              << " clientId=" << runtimeConfig.mqttConfig.clientId << '\n';
    std::cout << "  filestore: enabled="
              << (runtimeConfig.valueServiceConfig.fileStoreEnabled ? "1" : "0")
              << " host=" << runtimeConfig.valueServiceConfig.fileStoreHost
              << ':' << runtimeConfig.valueServiceConfig.fileStorePort
              << " keyPath=" << runtimeConfig.valueServiceConfig.valuesKeyPath << '\n';
    std::cout << "  topics: monitorPrefix=" << runtimeConfig.valueServiceConfig.monitorTopicPrefix
              << " subscribeQos=" << static_cast<int>(runtimeConfig.valueServiceConfig.subscribeQos)
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

    yaha::ValueServiceClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};
    if (!yaha::tryLoadValueServiceClientRuntimeConfigFromIni(
            configDocument,
            runtimeConfig,
            errorMessage)) {
        std::cerr << "Failed to load ValueService config from '" << cliOptions.configPath.string()
                  << "': " << errorMessage << '\n';
        return 1;
    }

    runtimeConfig.mqttConfig.enableMessageTrace = cliOptions.enableMessageTrace;

    printStartupConfiguration(cliOptions.configPath, runtimeConfig);

    yaha::ValueServiceComponent component{std::move(runtimeConfig.valueServiceConfig)};
    yaha::YahaMqttClient mqttClient{
        std::move(runtimeConfig.mqttConfig),
        component,
        yaha::makeBrokerTransport()};

    yaha::YahaMqttClientRuntime runtime{mqttClient, component};
    runtime.runUntilSignal();

    return 0;
}
