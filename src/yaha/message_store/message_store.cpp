#include "yaha/message_store/message_store.h"

#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace yaha {

MessageStore::MessageStore(MessageStoreConfig config)
    : config_(std::move(config))
    , tree_(config_.treeConfig)
    , persistence_(config_.persistenceConfig) {}

MessageStore::~MessageStore() {
    close();
}

SubscriptionMap MessageStore::getSubscriptions() const {
    return config_.subscriptions;
}

void MessageStore::handleMessage(const Message& message) {
    Message::validate(message);

    if (message.topic() == config_.cleanupTopic) {
        std::uint32_t days = 0U;
        if (tryParseCleanupDays(message.value(), days)) {
            std::lock_guard<std::mutex> lock{treeStateMutex_};
            (void)tree_.cleanup(days);
        }
        return;
    }

    std::lock_guard<std::mutex> lock{treeStateMutex_};
    tree_.addData(message);
}

void MessageStore::run() {
    {
        std::lock_guard<std::mutex> lock{lifecycleStateMutex_};
        if (running_) {
            return;
        }
        running_ = true;
    }

    {
        std::lock_guard<std::mutex> lock{treeStateMutex_};
        (void)persistence_.restoreLatest(tree_);
    }

    if (config_.httpStartCallback) {
        config_.httpStartCallback();
    }

    if (config_.persistenceConfig.intervalMs > 0U) {
        std::lock_guard<std::mutex> lock{treeStateMutex_};
        persistence_.startPeriodic(tree_);
    }
}

void MessageStore::close() {
    {
        std::lock_guard<std::mutex> lock{lifecycleStateMutex_};
        if (!running_) {
            return;
        }
        running_ = false;
    }

    if (config_.httpStopCallback) {
        config_.httpStopCallback();
    }

    persistence_.stopPeriodic();

    std::lock_guard<std::mutex> lock{treeStateMutex_};
    (void)persistence_.persistNow(tree_);
}

bool MessageStore::isRunning() const {
    std::lock_guard<std::mutex> lock{lifecycleStateMutex_};
    return running_;
}

std::vector<MessageTreeNode>
MessageStore::querySection(const std::string& topicPrefix,
                           std::uint32_t levelAmount,
                           bool includeHistory,
                           bool includeReason) const {
    std::lock_guard<std::mutex> lock{treeStateMutex_};
    return tree_.getSection(topicPrefix, levelAmount, includeHistory, includeReason);
}

bool MessageStore::tryParseCleanupDays(const Value& value, std::uint32_t& days) {
    if (std::holds_alternative<double>(value)) {
        const double number = std::get<double>(value);
        if (!std::isfinite(number) || number < 0.0) {
            return false;
        }
        days = static_cast<std::uint32_t>(number);
        return true;
    }

    const std::string& text = std::get<std::string>(value);
    if (text.empty()) {
        return false;
    }

    char* endPtr = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &endPtr, 10);
    if (endPtr == nullptr || *endPtr != '\0') {
        return false;
    }

    days = static_cast<std::uint32_t>(parsed);
    return true;
}

} // namespace yaha
