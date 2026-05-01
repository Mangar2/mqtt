#pragma once

/**
 * @file source_lifecycle_manager.h
 * @brief Source lifecycle manager for Broker Connector Phase 2.
 */

#include "yaha/broker_connector/source_http_adapter.h"

#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace yaha {

/**
 * @brief Runtime settings for source-side lifecycle loop.
 */
struct SourceLifecycleConfig {
    std::chrono::milliseconds reconnectDelay{1000};
    std::chrono::milliseconds loopSleep{20};
    std::chrono::milliseconds keepAliveInterval{30000};
    bool enableTrace{true};
};

/**
 * @brief Background loop that keeps source adapter connected and subscribed.
 */
class SourceLifecycleManager {
public:
    /**
     * @brief Constructs lifecycle manager for one source adapter.
     * @param adapter Source adapter instance.
     * @param config Loop timing configuration.
     */
    SourceLifecycleManager(SourceHttpBrokerAdapter& adapter, SourceLifecycleConfig config);

    /**
     * @brief Destructor that closes the lifecycle manager.
     */
    ~SourceLifecycleManager();

    SourceLifecycleManager(const SourceLifecycleManager&) = delete;
    SourceLifecycleManager& operator=(const SourceLifecycleManager&) = delete;

    /**
     * @brief Starts lifecycle thread.
     */
    void run();

    /**
     * @brief Stops lifecycle thread and closes source adapter.
     */
    void close();

    /**
     * @brief Returns whether lifecycle worker is running.
     * @return True while running.
     */
    [[nodiscard]] bool isRunning() const;

private:
    void workerLoop();
    void trace(const std::string& text) const;

    SourceHttpBrokerAdapter& adapter_;
    SourceLifecycleConfig config_;

    mutable std::mutex stateMutex_{};
    bool running_{false};
    std::thread workerThread_{};
};

} // namespace yaha
