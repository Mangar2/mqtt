#pragma once

/**
 * @file broker_connector_runtime.h
 * @brief Runtime orchestration for YAHA Broker Connector standalone process.
 */

#include "yaha/broker_connector/receiver_publish_port.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

namespace yaha {

/**
 * @brief Runtime control port for source-side lifecycle.
 */
class SourceRuntimePort {
public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~SourceRuntimePort();

    /**
     * @brief Starts source runtime.
     */
    virtual void run() = 0;

    /**
     * @brief Stops source runtime.
     */
    virtual void close() = 0;

protected:
    /**
     * @brief Protected default constructor.
     */
    SourceRuntimePort() = default;
};

/**
 * @brief Runtime control port for connector component lifecycle.
 */
class ConnectorRuntimePort {
public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~ConnectorRuntimePort();

    /**
     * @brief Starts component runtime.
     */
    virtual void run() = 0;

    /**
     * @brief Stops component runtime.
     */
    virtual void close() = 0;

protected:
    /**
     * @brief Protected default constructor.
     */
    ConnectorRuntimePort() = default;
};

/**
 * @brief Runtime orchestrator for Broker Connector standalone process.
 */
class BrokerConnectorClientRuntime {
public:
    /**
     * @brief Constructs runtime orchestrator around source, receiver, and connector ports.
     * @param receiverPort Receiver publish port.
     * @param sourceRuntime Source runtime control port.
     * @param connectorRuntime Connector runtime control port.
     */
    BrokerConnectorClientRuntime(ReceiverPublishPort& receiverPort,
                                 SourceRuntimePort& sourceRuntime,
                                 ConnectorRuntimePort& connectorRuntime);

    /**
     * @brief Starts runtime and blocks until SIGINT or SIGTERM.
     * @param errorMessage Human-readable failure text if startup fails.
     * @return True when startup and shutdown completed cleanly.
     */
    [[nodiscard]] bool runUntilSignal(std::string& errorMessage);

    /**
     * @brief Starts runtime with deterministic start order.
     * @param errorMessage Human-readable failure text if startup fails.
     * @return True when runtime started.
     */
    [[nodiscard]] bool start(std::string& errorMessage);

    /**
     * @brief Stops runtime with deterministic shutdown order.
     */
    void close();

    /**
     * @brief Returns whether runtime is started.
     * @return True while runtime is active.
     */
    [[nodiscard]] bool isRunning() const;

private:
    static void handleSignal(int signalNumber);

    ReceiverPublishPort& receiverPort_;
    SourceRuntimePort& sourceRuntime_;
    ConnectorRuntimePort& connectorRuntime_;

    std::chrono::milliseconds pollInterval_{100};
    mutable std::mutex runtime_state_mutex_{};
    bool running_{false};

    static std::atomic<bool> shutdownRequested_;
};

} // namespace yaha
