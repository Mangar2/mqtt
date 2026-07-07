#include "yaha/mqtt_client/broker_transport.h"
#include "yaha/value_service/value_service_component.h"
#include "yaha/value_service_client/value_service_client_app.h"
#include "httplib.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace {

std::atomic<bool> g_shutdownRequested{false};

constexpr std::string_view k_status_topic{"$MONITOR/valueservice/status"};
constexpr auto k_runtime_poll_interval = std::chrono::milliseconds{100};
constexpr int k_http_ok_status = 200;

void handleSignal(const int signalNumber) {
    (void)signalNumber;
    g_shutdownRequested.store(true);
}

void configureFileStoreClientTimeouts(httplib::Client* client) {
    if (client == nullptr) {
        return;
    }

    client->set_connection_timeout(1, 0);
    client->set_read_timeout(1, 0);
    client->set_write_timeout(1, 0);
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

bool isFileStoreDataAvailable(const yaha::ValueServiceConfig& config) {
    if (!config.fileStoreEnabled) {
        return true;
    }

    httplib::Client client{config.fileStoreHost, static_cast<int>(config.fileStorePort)};
    configureFileStoreClientTimeouts(&client);
    const auto response = client.Get(config.valuesKeyPath);
    return response && response->status == k_http_ok_status;
}

bool waitForFileStoreStartupData(const yaha::ValueServiceConfig& config) {
    if (!config.fileStoreEnabled) {
        return true;
    }

    const std::uint32_t maxAttempts = config.fileStoreStartupRetryCount + 1U;
    for (std::uint32_t attemptIndex = 0U; attemptIndex < maxAttempts; ++attemptIndex) {
        if (isFileStoreDataAvailable(config)) {
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
    std::cout << "Usage: yahavalueserviceclient [config-path] [--trace-messages] [--help]\n"
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
        configDocument = yaha::IniDocument::loadFromFile(cliOptions.configPath, "value_service_client");
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
    runtimeConfig.mqttConfig.willEnabled = true;
    runtimeConfig.mqttConfig.willTopic = std::string{k_status_topic};
    runtimeConfig.mqttConfig.willValue = std::string{"terminated"};
    runtimeConfig.mqttConfig.willQos = yaha::Qos::AtLeastOnce;
    runtimeConfig.mqttConfig.willRetain = true;

    printStartupConfiguration(cliOptions.configPath, runtimeConfig);

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    g_shutdownRequested.store(false);

    const yaha::ValueServiceConfig startupValueServiceConfig = runtimeConfig.valueServiceConfig;
    yaha::ValueServiceComponent component{std::move(runtimeConfig.valueServiceConfig)};
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

        if (!waitForFileStoreStartupData(startupValueServiceConfig)) {
            publishStatus(mqttClient, "stopped");
            mqttClient.close();
            component.close();
            return 2;
        }

        component.run();
        publishStatus(mqttClient, "running");
    } catch (const std::exception& exceptionValue) {
        std::cerr << "value_service[error] startup failed: " << exceptionValue.what() << '\n';
        try {
            publishStatus(mqttClient, "stopped");
        } catch (...) {
        }
        mqttClient.close();
        component.close();
        return 2;
    } catch (...) {
        std::cerr << "value_service[error] startup failed: unknown" << '\n';
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
