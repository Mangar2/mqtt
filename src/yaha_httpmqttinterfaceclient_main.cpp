#include "yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.h"

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

    yaha::HttpMqttInterfaceClientConfig runtimeConfig{};
    std::string configError{};
    if (!yaha::tryLoadHttpMqttInterfaceClientConfigFromIni(
            configDocument,
            runtimeConfig,
            configError)) {
        std::cerr << "Failed to parse HTTP MQTT interface client config from '"
                  << cliOptions.configPath.string() << "': " << configError << '\n';
        return 1;
    }

    return yaha::runHttpMqttInterfaceClient(runtimeConfig);
}
