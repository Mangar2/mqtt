#include "yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.h"
#include "yaha/error_handling/yaha_error.h"
#include "yaha/mqtt_client/broker_transport.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

struct CliOptions {
    std::filesystem::path configPath{"broker.ini"};
    bool configPathProvided{false};
    bool showHelp{false};
};

void printUsage() {
    std::cout << "Usage: yahahttpmqttinterfaceclient [config-path] [--help]\n"
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

} // namespace

int main(int argc, char* argv[]) {
    CliOptions cliOptions{};
    std::string cliError{};
    if (!tryParseCli(argc, argv, cliOptions, cliError)) {
        const yaha::YahaError yahaError{
            "HTTP_MQTT_CLIENT_CLI_PARSE_FAILED",
            cliError,
            "failed to parse arguments",
        };
        std::cerr << yahaError.buildMessage() << '\n';
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
        const yaha::YahaError yahaError{
            "HTTP_MQTT_CLIENT_CONFIG_LOAD_FAILED",
            exceptionValue.what(),
            "failed to load config file",
            "path=" + cliOptions.configPath.string(),
        };
        std::cerr << yahaError.buildMessage() << '\n';
        return 1;
    }

    yaha::HttpMqttInterfaceClientConfig runtimeConfig{};
    std::string configError{};
    if (!yaha::tryLoadHttpMqttInterfaceClientConfigFromIni(
            configDocument,
            runtimeConfig,
            configError)) {
        const yaha::YahaError yahaError{
            "HTTP_MQTT_CLIENT_CONFIG_PARSE_FAILED",
            configError,
            "failed to parse http mqtt interface client config",
            "path=" + cliOptions.configPath.string(),
        };
        std::cerr << yahaError.buildMessage() << '\n';
        return 1;
    }

    try {
        yaha::HttpMqttInterfaceClientComponent component{runtimeConfig};
        yaha::YahaMqttClient mqttClient{
            runtimeConfig.mqttConfig,
            component,
            yaha::makeBrokerTransport()};
        yaha::YahaMqttClientRuntime runtime{mqttClient, component};
        runtime.runUntilSignal();
    } catch (const yaha::YahaError& yahaError) {
        std::cerr << yahaError.buildMessage() << '\n';
        return 1;
    } catch (const std::exception& exceptionValue) {
        const yaha::YahaError yahaError{
            "HTTP_MQTT_CLIENT_RUNTIME_FAILED",
            exceptionValue.what(),
            "http mqtt interface client runtime failed",
        };
        std::cerr << yahaError.buildMessage() << '\n';
        return 1;
    }

    return 0;
}
