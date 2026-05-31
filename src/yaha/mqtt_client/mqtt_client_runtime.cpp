#include "yaha/mqtt_client/mqtt_client_runtime.h"

#include <atomic>
#include <csignal>
#include <thread>

namespace yaha {

namespace {

std::atomic<bool> shutdownRequested{false};

void handleSignal(const int signalNumber) {
    (void)signalNumber;
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

    mqttClient_.run();

    // Start component only after the MQTT session is connected so startup
    // status publishes are not rejected as "disconnected".
    while (!shutdownRequested.load() && mqttClient_.isRunning() && !mqttClient_.isConnected()) {
        std::this_thread::sleep_for(pollInterval_);
    }

    if (!shutdownRequested.load() && mqttClient_.isRunning()) {
        component_.run();
    }

    while (!shutdownRequested.load()) {
        std::this_thread::sleep_for(pollInterval_);
    }

    component_.close();
    mqttClient_.close();
}

} // namespace yaha
