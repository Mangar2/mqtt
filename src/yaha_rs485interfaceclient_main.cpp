#include "yaha/rs485_interface_client/rs485_interface_client_app.h"
#include "yaha/error_handling/yaha_error.h"

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

bool parseCli(const int argc, char* const* argv, CliOptions& options, std::string& errorText) {
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
    if (!parseCli(argc, argv, cliOptions, cliError)) {
        std::cerr << yaha::YahaError{
            "RS485_MAIN_CLI_PARSE_FAILED",
            "failed to parse command line arguments",
            "Invalid command line arguments for RS485 client.",
            cliError}
                         .buildMessage()
                  << '\n';
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
        std::cerr << yaha::YahaError{
            "RS485_MAIN_CONFIG_LOAD_FAILED",
            "failed to load INI config file",
            "Failed to load RS485 client configuration file.",
            exceptionValue.what()}
                         .buildMessage()
                  << '\n';
        return 1;
    }

    yaha::Rs485InterfaceRuntimeConfig runtimeConfig{};
    try {
        runtimeConfig = yaha::loadRs485InterfaceClientRuntimeConfigFromIni(configDocument);
    } catch (const yaha::YahaError& exceptionValue) {
        std::cerr << exceptionValue.buildMessage() << '\n';
        return 1;
    }

    runtimeConfig.mqttConfig.enableMessageTrace = cliOptions.enableMessageTrace;
    printStartupSummary(cliOptions.configPath, runtimeConfig);

    yaha::Rs485InterfaceClientRuntimeObjects runtimeObjects{};
    try {
        runtimeObjects = yaha::buildRs485InterfaceClientRuntime(std::move(runtimeConfig));
    } catch (const yaha::YahaError& exceptionValue) {
        std::cerr << exceptionValue.buildMessage() << '\n';
        return 1;
    }

    runtimeObjects.runtime->runUntilSignal();

    return 0;
}
