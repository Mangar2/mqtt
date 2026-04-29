#include "yaha/message_store_client/message_store_client_app.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> shutdownRequested{false};

void handleSignal(int) {
    shutdownRequested.store(true);
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

    yaha::MessageStoreClientApp app{std::move(runtimeConfig)};

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    app.run();
    while (!shutdownRequested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    app.close();
    return 0;
}
