#include "yaha/rs485_interface_client/rs485_interface_client_app.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

struct CliOptions {
    std::filesystem::path configPath{"broker.ini"};
    bool configPathProvided{false};
    bool enableMessageTrace{false};
    bool showHelp{false};
};

void printUsage() {
    std::cout << "Usage: yahars485interfaceclient [config-path] [--trace-messages] [--help]\n"
              << "  config-path         optional INI config file (default: broker.ini)\n"
              << "  --trace-messages    print sent/received MQTT messages\n"
              << "  --help              print this help and exit\n"
              << std::flush;
}

bool tryParseCli(const int argc, char* const* argv, CliOptions& options, std::string& errorText) {
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

void printStartupSummary(
    const std::filesystem::path& configPath,
    const yaha::Rs485InterfaceRuntimeConfig& runtimeConfig) {
    std::cout << "yahars485interfaceclient\n";
    std::cout << "  config: " << configPath.string() << '\n';
    std::cout << "  mqtt: " << runtimeConfig.mqttConfig.brokerHost << ':'
              << runtimeConfig.mqttConfig.brokerPort
              << " clientId=" << runtimeConfig.mqttConfig.clientId << '\n';
    std::cout << "  rs485: serialPortName=" << runtimeConfig.rs485Config.serialPortName
              << " baudrate=" << runtimeConfig.rs485Config.baudrate
              << " myAddress=" << static_cast<int>(runtimeConfig.rs485Config.myAddress)
              << " maxVersion=" << static_cast<int>(runtimeConfig.rs485Config.maxVersion)
              << " tickDelayMs=" << runtimeConfig.rs485Config.tickDelayMs << '\n';
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

    yaha::Rs485InterfaceRuntimeConfig runtimeConfig{};
    std::string configError{};
    if (!yaha::tryLoadRs485InterfaceClientRuntimeConfigFromIni(configDocument, runtimeConfig, configError)) {
        std::cerr << "Failed to load rs485 interface config from '" << cliOptions.configPath.string()
                  << "': " << configError << '\n';
        return 1;
    }

    runtimeConfig.mqttConfig.enableMessageTrace = cliOptions.enableMessageTrace;
    printStartupSummary(cliOptions.configPath, runtimeConfig);

    yaha::Rs485InterfaceClientRuntimeObjects runtimeObjects{};
    std::string runtimeBuildError{};
    if (!yaha::tryBuildRs485InterfaceClientRuntime(
            std::move(runtimeConfig),
            runtimeObjects,
            runtimeBuildError)) {
        std::cerr << "Failed to build rs485 runtime: " << runtimeBuildError << '\n';
        return 1;
    }

    std::string serialOpenError{};
    if (!runtimeObjects.serialAdapter->open(
            runtimeObjects.rs485Config.serialPortName,
            runtimeObjects.rs485Config.baudrate,
            serialOpenError)) {
        std::cerr << "Failed to open serial adapter: " << serialOpenError << '\n';
        return 1;
    }

    runtimeObjects.runtime->runUntilSignal();
    runtimeObjects.serialAdapter->close();

    return 0;
}
