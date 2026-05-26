#include "yaha/error_handling/yaha_error.h"
#include "yaha/serial_device_client/serial_device_client_app.h"

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
    std::cout << "Usage: yahaserialdeviceclient [config-path] [--trace-messages] [--help]\n"
              << "  config-path         optional INI config file (default: broker.ini)\n"
              << "  --trace-messages    print sent/received MQTT messages\n"
              << "  --help              print this help and exit\n"
              << std::flush;
}

bool parseCli(const int argumentCount, char* const* argumentValues, CliOptions& options, std::string& errorText) {
    for (int argumentIndex = 1; argumentIndex < argumentCount; ++argumentIndex) {
        const std::string argument{argumentValues[argumentIndex]};
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
    const yaha::SerialDeviceClientRuntimeConfig& runtimeConfig) {
    std::cout << "yahaserialdeviceclient\n";
    std::cout << "  config: " << configPath.string() << '\n';
    std::cout << "  mqtt: " << runtimeConfig.mqttConfig.brokerHost << ':'
              << runtimeConfig.mqttConfig.brokerPort
              << " clientId=" << runtimeConfig.mqttConfig.clientId << '\n';
    std::cout << "  serialdevice: serialPortName=" << runtimeConfig.serialDeviceConfig.serialPortName
              << " baudrate=" << runtimeConfig.serialDeviceConfig.baudrate
              << " qos=" << static_cast<unsigned int>(runtimeConfig.serialDeviceConfig.subscribeQos)
              << " trace=" << runtimeConfig.serialDeviceConfig.traceLevel
              << " keepAliveDelayInSeconds=" << runtimeConfig.serialDeviceConfig.keepAliveDelayInSeconds << '\n';
    std::cout << "  logging: mqttTrace=" << (runtimeConfig.mqttConfig.enableMessageTrace ? "1" : "0") << '\n';
    std::cout << std::flush;
}

} // namespace

int main(int argumentCount, char* argumentValues[]) {
    CliOptions cliOptions{};
    std::string cliError{};
    if (!parseCli(argumentCount, argumentValues, cliOptions, cliError)) {
        std::cerr << yaha::YahaError{
            "SERIALDEVICE_MAIN_CLI_PARSE_FAILED",
            "failed to parse command line arguments",
            "Invalid command line arguments for SerialDevice client.",
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
            "SERIALDEVICE_MAIN_CONFIG_LOAD_FAILED",
            "failed to load INI config file",
            "Failed to load SerialDevice client configuration file.",
            exceptionValue.what()}
                         .buildMessage()
                  << '\n';
        return 1;
    }

    yaha::SerialDeviceClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};
    if (!yaha::tryLoadSerialDeviceClientRuntimeConfigFromIni(configDocument, runtimeConfig, errorMessage)) {
        std::cerr << yaha::YahaError{
            "SERIALDEVICE_MAIN_CONFIG_PARSE_FAILED",
            "failed to parse INI config file",
            "Failed to parse SerialDevice client configuration.",
            errorMessage}
                         .buildMessage()
                  << '\n';
        return 1;
    }

    if (cliOptions.enableMessageTrace) {
        runtimeConfig.mqttConfig.enableMessageTrace = true;
    }

    printStartupSummary(cliOptions.configPath, runtimeConfig);

    yaha::SerialDeviceClientRuntimeObjects runtimeObjects{};
    try {
        runtimeObjects = yaha::buildSerialDeviceClientRuntime(runtimeConfig);
    } catch (const std::exception& exceptionValue) {
        std::cerr << yaha::YahaError{
            "SERIALDEVICE_MAIN_RUNTIME_BUILD_FAILED",
            "failed to build SerialDevice runtime",
            "Failed to initialize SerialDevice runtime.",
            exceptionValue.what()}
                         .buildMessage()
                  << '\n';
        return 1;
    }

    runtimeObjects.runtime->runUntilSignal();
    return 0;
}
