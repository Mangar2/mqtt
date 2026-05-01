#include "yaha/broker_connector_client/broker_connector_runtime.h"

#include <csignal>
#include <thread>

namespace yaha {

SourceRuntimePort::~SourceRuntimePort() = default;
ConnectorRuntimePort::~ConnectorRuntimePort() = default;

std::atomic<bool> BrokerConnectorClientRuntime::shutdownRequested_{false};

BrokerConnectorClientRuntime::BrokerConnectorClientRuntime(ReceiverPublishPort& receiverPort,
                                                           SourceRuntimePort& sourceRuntime,
                                                           ConnectorRuntimePort& connectorRuntime)
    : receiverPort_(receiverPort)
    , sourceRuntime_(sourceRuntime)
    , connectorRuntime_(connectorRuntime) {}

bool BrokerConnectorClientRuntime::runUntilSignal(std::string& errorMessage) {
    if (!start(errorMessage)) {
        return false;
    }

    shutdownRequested_.store(false);
    std::signal(SIGINT, &BrokerConnectorClientRuntime::handleSignal);
    std::signal(SIGTERM, &BrokerConnectorClientRuntime::handleSignal);

    while (!shutdownRequested_.load()) {
        std::this_thread::sleep_for(pollInterval_);
    }

    close();
    return true;
}

bool BrokerConnectorClientRuntime::start(std::string& errorMessage) {
    std::lock_guard<std::mutex> lock{runtime_state_mutex_};
    if (running_) {
        return true;
    }

    if (!receiverPort_.start(errorMessage)) {
        return false;
    }

    connectorRuntime_.run();
    sourceRuntime_.run();

    running_ = true;
    return true;
}

void BrokerConnectorClientRuntime::close() {
    std::lock_guard<std::mutex> lock{runtime_state_mutex_};
    if (!running_) {
        return;
    }

    sourceRuntime_.close();
    receiverPort_.close();
    connectorRuntime_.close();

    running_ = false;
}

bool BrokerConnectorClientRuntime::isRunning() const {
    std::lock_guard<std::mutex> lock{runtime_state_mutex_};
    return running_;
}

void BrokerConnectorClientRuntime::handleSignal(const int signalNumber) {
    (void)signalNumber;
    shutdownRequested_.store(true);
}

} // namespace yaha
