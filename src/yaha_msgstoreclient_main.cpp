#include "yaha/message_store_client/message_store_client_app.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> shutdownRequested{false};

void handleSignal(int) {
    shutdownRequested.store(true);
}

const char* qosToText(const yaha::Qos qos) {
    switch (qos) {
        case yaha::Qos::AtMostOnce:
            return "0";
        case yaha::Qos::AtLeastOnce:
            return "1";
        case yaha::Qos::ExactlyOnce:
            return "2";
    }

    return "0";
}

void printStartupConfiguration(const std::filesystem::path& configPath,
                               const yaha::MessageStoreClientRuntimeConfig& runtimeConfig) {
    std::cout << "yahamsgstoreclient\n";
    std::cout << "  config: " << configPath.string() << '\n';
    std::cout << "  mqtt: " << runtimeConfig.mqttConfig.brokerHost << ':'
              << runtimeConfig.mqttConfig.brokerPort
              << " clientId=" << runtimeConfig.mqttConfig.clientId << '\n';
    std::cout << "  http: " << runtimeConfig.storeConfig.serverHost << ':'
              << runtimeConfig.storeConfig.serverPort << runtimeConfig.storeConfig.serverPath << '\n';
    std::cout << "  persist: " << runtimeConfig.storeConfig.persistenceConfig.directory << '/'
              << runtimeConfig.storeConfig.persistenceConfig.filename
              << " intervalMs=" << runtimeConfig.storeConfig.persistenceConfig.intervalMs
              << " keepFiles=" << runtimeConfig.storeConfig.persistenceConfig.keepFiles << '\n';
    std::cout << "  subscriptions:";
    for (const auto& subscription : runtimeConfig.storeConfig.subscriptions) {
        std::cout << " [" << subscription.first << "=>qos" << qosToText(subscription.second) << ']';
    }
    std::cout << '\n';
}

void reportInitialConnection(yaha::MessageStoreClientApp& app) {
    constexpr std::uint32_t maximumWaitCycles = 50U;
    for (std::uint32_t waitCycle = 0U; waitCycle < maximumWaitCycles; ++waitCycle) {
        if (app.isConnected()) {
            std::cout << "  broker: connected\n";
            return;
        }

        if (shutdownRequested.load()) {
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    std::cout << "  broker: not connected yet, retry running in background\n";
}

} // namespace

int main(int argc, char* argv[]) {
    const std::filesystem::path configPath =
        (argc >= 2) ? std::filesystem::path{argv[1]} : std::filesystem::path{"broker.ini"};

    yaha::MessageStoreClientRuntimeConfig runtimeConfig{};
    std::string errorMessage{};
    if (!yaha::MessageStoreClientApp::tryLoadConfigFromFile(configPath, runtimeConfig, errorMessage)) {
        std::cerr << "Failed to load MessageStore config from '" << configPath.string()
                  << "': " << errorMessage << '\n';
        return 1;
    }

    const std::string configuredHttpHost = runtimeConfig.storeConfig.serverHost;
    const std::string configuredHttpPath = runtimeConfig.storeConfig.serverPath;
    const std::uint16_t configuredHttpPort = runtimeConfig.storeConfig.serverPort;

    printStartupConfiguration(configPath, runtimeConfig);

    yaha::MessageStoreClientApp app{std::move(runtimeConfig)};

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    app.run();
    std::cout << "  runtime: started\n";
    if (configuredHttpPort == 0U) {
        std::cout << "  http: disabled (server.port=0)\n";
    } else {
        const std::string effectiveHost = configuredHttpHost.empty() ? "127.0.0.1" : configuredHttpHost;
        std::cout << "  http: listening on http://" << effectiveHost << ':' << configuredHttpPort
                  << configuredHttpPath << "\n";
    }
    reportInitialConnection(app);
    std::cout << "  signal: waiting for SIGINT/SIGTERM\n";
    std::cout << std::flush;

    while (!shutdownRequested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    std::cout << "  signal: received, disconnecting\n";
    std::cout << "  runtime: shutting down\n";
    app.close();
    std::cout << "  runtime: stopped\n";
    std::cout << std::flush;
    return 0;
}
