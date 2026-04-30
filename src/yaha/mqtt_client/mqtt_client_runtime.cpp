#include "yaha/mqtt_client/mqtt_client_runtime.h"

#include <atomic>
#include <csignal>
#include <thread>

namespace yaha {

namespace {

std::atomic<bool> shutdownRequested{false};

void handleSignal(int) {
    shutdownRequested.store(true);
}

} // namespace

YahaMqttClientRuntime::YahaMqttClientRuntime(YahaMqttClient& mqttClient, IMqttComponent& component)
    : mqttClient_(mqttClient)
    , component_(component) {}

void YahaMqttClientRuntime::runUntilSignal() {
    shutdownRequested.store(false);
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    component_.run();
    mqttClient_.run();

    while (!shutdownRequested.load()) {
        std::this_thread::sleep_for(pollInterval_);
    }

    mqttClient_.close();
    component_.close();
}

} // namespace yaha
