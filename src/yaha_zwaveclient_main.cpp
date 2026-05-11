#include "yaha/mqtt_client/broker_transport.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"
#include "yaha/zwave/zwave_service_component.h"
#include "yaha/zwave_client/openzwave_runtime_driver_port.h"
#include "yaha/zwave_client/zwave_client_app.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
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
    std::cout << "Usage: yahazwaveclient [config-path] [--trace-messages] [--help]\n"
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

void printStartupConfiguration(
    const std::filesystem::path& configPath,
    const yaha::ZwaveClientRuntimeConfig& runtimeConfig) {
    std::cout << "yahazwaveclient\n";
    std::cout << "  config: " << configPath.string() << '\n';
    std::cout << "  mqtt: " << runtimeConfig.mqttConfig.brokerHost << ':'
              << runtimeConfig.mqttConfig.brokerPort
              << " clientId=" << runtimeConfig.mqttConfig.clientId << '\n';
    std::cout << "  zwave: usbDevice=" << runtimeConfig.zwaveConfig.usb.device
              << " usbTopic=" << runtimeConfig.zwaveConfig.usb.topic
              << " devices=" << runtimeConfig.zwaveConfig.devices.size() << '\n';
    std::cout << "  qos: subscribe=" << static_cast<int>(runtimeConfig.zwaveConfig.subscribeQos)
              << " publish=" << static_cast<int>(runtimeConfig.zwaveConfig.qos)
              << " retain=" << (runtimeConfig.zwaveConfig.retain ? "1" : "0") << '\n';
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

    yaha::ZwaveClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};
    if (!yaha::tryLoadZwaveClientRuntimeConfigFromIni(
            configDocument,
            runtimeConfig,
            errorMessage)) {
        std::cerr << "Failed to load ZWave config from '" << cliOptions.configPath.string()
                  << "': " << errorMessage << '\n';
        return 1;
    }

    runtimeConfig.mqttConfig.enableMessageTrace = cliOptions.enableMessageTrace;

    printStartupConfiguration(cliOptions.configPath, runtimeConfig);

    auto driverPort = std::make_shared<yaha::OpenZwaveRuntimeDriverPort>(runtimeConfig.zwaveConfig.usb.device);
    auto controller = std::make_shared<yaha::ZwaveController>(runtimeConfig.zwaveConfig.usb, *driverPort);
    driverPort->bindController(*controller);
    driverPort->start();

    yaha::ZwaveServiceComponent component{runtimeConfig.zwaveConfig, controller};

    yaha::YahaMqttClient mqttClient{
        std::move(runtimeConfig.mqttConfig),
        component,
        yaha::makeBrokerTransport()};

    yaha::YahaMqttClientRuntime runtime{mqttClient, component};
    runtime.runUntilSignal();

    return 0;
}