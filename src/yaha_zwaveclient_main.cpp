#include "yaha/mqtt_client/broker_transport.h"
#include "yaha/zwave/zwave_service_component.h"
#include "yaha/zwave_client/openzwave_runtime_driver_port.h"
#include "yaha/zwave_client/zwave_client_app.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace {

std::atomic<bool> g_shutdownRequested{false};

constexpr std::string_view k_status_topic{"$MONITOR/zwave/status"};
constexpr auto k_runtime_poll_interval = std::chrono::milliseconds{100};

void handleSignal(const int signalNumber) {
    (void)signalNumber;
    g_shutdownRequested.store(true);
}

bool waitForBrokerConnection(yaha::YahaMqttClient& mqttClient) {
    while (!g_shutdownRequested.load() && mqttClient.isRunning() && !mqttClient.isConnected()) {
        std::this_thread::sleep_for(k_runtime_poll_interval);
    }

    return mqttClient.isConnected();
}

void publishStatus(yaha::YahaMqttClient& mqttClient, const std::string& statusText) {
    mqttClient.publish(yaha::Message{
        std::string{k_status_topic},
        statusText,
        yaha::Qos::AtLeastOnce,
        true});
}

bool waitForFileStoreStartupSync(yaha::ZwaveConfig& config) {
    if (!config.fileStoreEnabled) {
        return true;
    }

    const std::uint32_t maxAttempts = config.fileStoreStartupRetryCount + 1U;
    for (std::uint32_t attemptIndex = 0U; attemptIndex < maxAttempts; ++attemptIndex) {
        std::string syncErrorMessage{};
        if (yaha::trySyncZwaveDeviceSettingsFromFileStore(config, syncErrorMessage)) {
            return true;
        }

        if (attemptIndex + 1U >= maxAttempts || g_shutdownRequested.load()) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::seconds{config.fileStoreStartupRetryIntervalSeconds});
    }

    return false;
}

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
    std::cout << "  logging: level=" << static_cast<int>(runtimeConfig.zwaveConfig.logLevel)
              << " incoming="
              << (runtimeConfig.zwaveConfig.logIncomingMessages ? "1" : "0")
              << " outgoing="
              << (runtimeConfig.zwaveConfig.logOutgoingMessages ? "1" : "0")
              << " pollIntervalMs=" << runtimeConfig.zwaveConfig.pollIntervalMs
              << " commandReactionPollIntervalMs=" << runtimeConfig.zwaveConfig.commandReactionPollIntervalMs
              << " commandReactionTimeoutMs=" << runtimeConfig.zwaveConfig.commandReactionTimeoutMs
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
    runtimeConfig.mqttConfig.willEnabled = true;
    runtimeConfig.mqttConfig.willTopic = std::string{k_status_topic};
    runtimeConfig.mqttConfig.willValue = std::string{"terminated"};
    runtimeConfig.mqttConfig.willQos = yaha::Qos::AtLeastOnce;
    runtimeConfig.mqttConfig.willRetain = true;

    printStartupConfiguration(cliOptions.configPath, runtimeConfig);

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    g_shutdownRequested.store(false);

    auto driverPort = std::make_shared<yaha::OpenZwaveRuntimeDriverPort>(
        runtimeConfig.zwaveConfig.usb.device,
        runtimeConfig.zwaveConfig.logLevel,
        runtimeConfig.zwaveConfig.pollIntervalMs);
    auto controller = std::make_shared<yaha::ZwaveController>(
        runtimeConfig.zwaveConfig.usb,
        *driverPort,
        runtimeConfig.zwaveConfig.pollIntervalMs,
        runtimeConfig.zwaveConfig.commandReactionPollIntervalMs,
        runtimeConfig.zwaveConfig.commandReactionTimeoutMs);
    controller->setDriverFailedCallback([] {
        std::cerr << "fatal: OpenZWave reported driver failure, terminating process for systemd restart\n";
        std::cerr << std::flush;
        std::exit(2);
    });
    controller->setUnresponsiveNetworkCallback([] {
        std::cerr
            << "fatal: OpenZWave input unresponsive (>=100 timeout drops and >=3min without successful input), "
            << "terminating process for systemd restart\n";
        std::cerr << std::flush;
        std::exit(2);
    });
    driverPort->bindController(*controller);
    driverPort->start();

    yaha::ZwaveServiceComponent component{runtimeConfig.zwaveConfig, controller};

    yaha::YahaMqttClient mqttClient{
        std::move(runtimeConfig.mqttConfig),
        component,
        yaha::makeBrokerTransport()};

    mqttClient.run();
    if (!waitForBrokerConnection(mqttClient)) {
        mqttClient.close();
        component.close();
        return 2;
    }

    try {
        publishStatus(mqttClient, "starting");

        if (!waitForFileStoreStartupSync(runtimeConfig.zwaveConfig)) {
            publishStatus(mqttClient, "stopped");
            mqttClient.close();
            component.close();
            return 2;
        }

        component.setDeviceConfiguration(runtimeConfig.zwaveConfig.devices);
        component.run();
        publishStatus(mqttClient, "running");
    } catch (const std::exception& exceptionValue) {
        std::cerr << "zwave_client[error] startup failed: " << exceptionValue.what() << '\n';
        try {
            publishStatus(mqttClient, "stopped");
        } catch (...) {
        }
        mqttClient.close();
        component.close();
        return 2;
    } catch (...) {
        std::cerr << "zwave_client[error] startup failed: unknown" << '\n';
        try {
            publishStatus(mqttClient, "stopped");
        } catch (...) {
        }
        mqttClient.close();
        component.close();
        return 2;
    }

    while (!g_shutdownRequested.load()) {
        std::this_thread::sleep_for(k_runtime_poll_interval);
    }

    try {
        publishStatus(mqttClient, "stopped");
    } catch (...) {
    }

    component.close();
    mqttClient.close();

    return 0;
}