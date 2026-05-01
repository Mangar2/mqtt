#include "yaha/broker_connector/source_lifecycle_manager.h"

#include <iostream>
#include <string>
#include <thread>

namespace yaha {

SourceLifecycleManager::SourceLifecycleManager(SourceHttpBrokerAdapter& adapter,
                                               SourceLifecycleConfig config)
    : adapter_(adapter)
    , config_(config) {}

SourceLifecycleManager::~SourceLifecycleManager() {
    close();
}

void SourceLifecycleManager::run() {
    std::lock_guard<std::mutex> lock{stateMutex_};
    if (running_) {
        return;
    }

    running_ = true;
    workerThread_ = std::thread{&SourceLifecycleManager::workerLoop, this};
}

void SourceLifecycleManager::close() {
    {
        std::lock_guard<std::mutex> lock{stateMutex_};
        if (!running_) {
            adapter_.close();
            return;
        }
        running_ = false;
    }

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    adapter_.close();
}

bool SourceLifecycleManager::isRunning() const {
    std::lock_guard<std::mutex> lock{stateMutex_};
    return running_;
}

void SourceLifecycleManager::workerLoop() {
    auto lastPing = std::chrono::steady_clock::now();

    while (true) {
        {
            std::lock_guard<std::mutex> lock{stateMutex_};
            if (!running_) {
                break;
            }
        }

        if (!adapter_.isConnected()) {
            std::string connectError{};
            if (!adapter_.connectAndSubscribe(connectError)) {
                if (!connectError.empty()) {
                    trace("  source: connect failed: " + connectError);
                }
                std::this_thread::sleep_for(config_.reconnectDelay);
                continue;
            }

            if (connectError.empty()) {
                trace("  source: connected and subscribed");
            } else {
                trace("  source: connected and subscribed: " + connectError);
            }
            lastPing = std::chrono::steady_clock::now();
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - lastPing >= config_.keepAliveInterval) {
            std::string pingError{};
            if (!adapter_.ping(pingError)) {
                if (!pingError.empty()) {
                    trace("  source: ping failed: " + pingError);
                }
                std::this_thread::sleep_for(config_.reconnectDelay);
                continue;
            }
            lastPing = now;
        }

        std::this_thread::sleep_for(config_.loopSleep);
    }
}

void SourceLifecycleManager::trace(const std::string& text) const {
    if (!config_.enableTrace) {
        return;
    }

    std::cout << text << '\n' << std::flush;
}

} // namespace yaha
