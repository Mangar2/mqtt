#include "yaha/mqtt_client/broker_transport.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"
#include "yaha/pushover/pushover_component.h"
#include "yaha/pushover_client/pushover_client_app.h"

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
    std::cout << "Usage: yahapushoverclient [config-path] [--trace-messages] [--help]\\n"
              << "  config-path       optional INI config file (default: broker.ini)\\n"
              << "  --trace-messages  print sent/received MQTT messages\\n"
              << "  --help            print this help and exit\\n"
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

    yaha::PushoverClientRuntimeConfig runtimeConfig{};
    std::string configError{};
    if (!yaha::tryLoadPushoverClientRuntimeConfigFromIni(
            configDocument,
            runtimeConfig,
            configError)) {
        std::cerr << "Failed to load Pushover config from '" << cliOptions.configPath.string()
                  << "': " << configError << '\n';
        return 1;
    }

    const bool enableMessageTraceFromConfig =
        runtimeConfig.logIncomingMessages || runtimeConfig.logOutgoingMessages;
    runtimeConfig.mqttConfig.enableMessageTrace =
        cliOptions.enableMessageTrace || enableMessageTraceFromConfig;

    if (runtimeConfig.mqttConfig.enableMessageTrace && enableMessageTraceFromConfig && !cliOptions.enableMessageTrace) {
        std::cout << "  startup: message logging enabled via pushover.logIncomingMessages/logOutgoingMessages\n";
    }

    yaha::PushoverComponent component{
        runtimeConfig.pushoverConfig,
        yaha::makePushoverRequestSender(runtimeConfig.pushoverConfig)};
    yaha::YahaMqttClient mqttClient{
        runtimeConfig.mqttConfig,
        component,
        yaha::makeBrokerTransport()};
    yaha::YahaMqttClientRuntime runtime{mqttClient, component};
    runtime.runUntilSignal();

    return 0;
}
