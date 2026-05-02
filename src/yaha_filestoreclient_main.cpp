#include "yaha/file_store/file_store.h"
#include "yaha/file_store_client/file_store_client_app.h"
#include "yaha/mqtt_client/broker_transport.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"

#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>

namespace {

struct CliOptions {
    std::filesystem::path configPath{"broker.ini"};
    bool configPathProvided{false};
    bool enableMessageTrace{false};
    bool showHelp{false};
};

struct CliParseResult {
    bool success{false};
    CliOptions options{};
    std::string errorText{};
};

void printUsage() {
    std::cout << "Usage: yahafilestoreclient [config-path] [--trace-messages] [--help]\n"
              << "  config-path         optional INI config file (default: broker.ini)\n"
              << "  --trace-messages    print sent/received MQTT messages\n"
              << "  --help              print this help and exit\n"
              << std::flush;
}

CliParseResult tryParseCli(const int argc, char* argv[]) {
    CliParseResult result{};
    for (int argIndex = 1; argIndex < argc; ++argIndex) {
        const std::string argument{argv[argIndex]};
        if (argument == "--help" || argument == "-h") {
            result.options.showHelp = true;
            continue;
        }

        if (argument == "--trace-messages") {
            result.options.enableMessageTrace = true;
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            result.errorText = std::format("unknown argument: {}", argument);
            return result;
        }

        if (result.options.configPathProvided) {
            result.errorText = "multiple config paths provided";
            return result;
        }

        result.options.configPath = std::filesystem::path{argument};
        result.options.configPathProvided = true;
    }

    result.success = true;
    return result;
}

void printStartupConfiguration(const std::filesystem::path& configPath,
                               const yaha::FileStoreClientRuntimeConfig& runtimeConfig) {
    std::cout << "yahafilestoreclient\n";
    std::cout << "  config: " << configPath.string() << '\n';
    std::cout << "  mqtt: " << runtimeConfig.mqttConfig.brokerHost << ':'
              << runtimeConfig.mqttConfig.brokerPort
              << " clientId=" << runtimeConfig.mqttConfig.clientId << '\n';
    std::cout << "  http: " << runtimeConfig.storeConfig.serverHost << ':'
              << runtimeConfig.storeConfig.serverPort << '\n';
    std::cout << "  filestore: dir=" << runtimeConfig.storeConfig.directory.string()
              << " keepFiles=" << runtimeConfig.storeConfig.keepFiles
              << " maxKeyLength=" << runtimeConfig.storeConfig.maxKeyLength << '\n';
    std::cout << "  monitoring: enabled="
              << (runtimeConfig.storeConfig.monitoring.enabled ? "1" : "0")
              << " prefix=" << runtimeConfig.storeConfig.monitoring.topicPrefix
              << " intervalMs=" << runtimeConfig.storeConfig.monitoring.watchIntervalMs
              << '\n';
}

} // namespace

int main(int argc, char* argv[]) {
    const CliParseResult parseResult = tryParseCli(argc, argv);
    if (!parseResult.success) {
        std::cerr << "Failed to parse arguments: " << parseResult.errorText << '\n';
        printUsage();
        return 1;
    }

    CliOptions cliOptions = parseResult.options;

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

    const auto runtimeConfigResult = yaha::loadFileStoreClientRuntimeConfigFromIni(configDocument);
    if (!runtimeConfigResult.success) {
        std::cerr << "Failed to load FileStore config from '" << cliOptions.configPath.string()
                  << "': " << runtimeConfigResult.errorMessage << '\n';
        return 1;
    }

    auto runtimeConfig = runtimeConfigResult.config;

    runtimeConfig.mqttConfig.enableMessageTrace = cliOptions.enableMessageTrace;

    printStartupConfiguration(cliOptions.configPath, runtimeConfig);

    yaha::FileStore fileStore{std::move(runtimeConfig.storeConfig)};
    yaha::YahaMqttClient mqttClient{
        std::move(runtimeConfig.mqttConfig),
        fileStore,
        yaha::makeBrokerTransport()};

    yaha::YahaMqttClientRuntime runtime{mqttClient, fileStore};
    runtime.runUntilSignal();
    return 0;
}
