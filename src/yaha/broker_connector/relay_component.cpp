#include "yaha/broker_connector/relay_component.h"

#include <exception>
#include <thread>
#include <utility>

namespace yaha {

BrokerConnectorComponent::BrokerConnectorComponent(RelayPolicyConfig config)
    : config_(config) {}

BrokerConnectorComponent::~BrokerConnectorComponent() {
    close();
}

void BrokerConnectorComponent::setSourceAdapter(SourceHttpBrokerAdapter& sourceAdapter,
                                                SourceLifecycleConfig lifecycleConfig) {
    sourceAdapter.setIncomingPublishCallback(
        [this](const Message& message, const SourcePublishMeta& sourceMeta) {
            (void)onIncomingPublish(message, sourceMeta);
        });

    std::lock_guard<std::mutex> lock{relay_state_mutex_};
    sourceLifecycle_ = std::make_unique<SourceLifecycleManager>(sourceAdapter, lifecycleConfig);
}

SubscriptionMap BrokerConnectorComponent::getSubscriptions() const {
    return {};
}

void BrokerConnectorComponent::handleMessage(const Message& message) {
    (void)message;
}

void BrokerConnectorComponent::setPublishCallback(PublishCallback callback) {
    std::lock_guard<std::mutex> lock{relay_state_mutex_};
    publishCallback_ = std::move(callback);
}

void BrokerConnectorComponent::run() {
    SourceLifecycleManager* lifecycle = nullptr;
    {
        std::lock_guard<std::mutex> lock{relay_state_mutex_};
        if (running_) {
            return;
        }

        running_ = true;
        lifecycle = sourceLifecycle_.get();
    }

    if (lifecycle != nullptr) {
        lifecycle->run();
    }
}

void BrokerConnectorComponent::close() {
    SourceLifecycleManager* lifecycle = nullptr;
    {
        std::lock_guard<std::mutex> lock{relay_state_mutex_};
        running_ = false;
        lifecycle = sourceLifecycle_.get();
    }

    if (lifecycle != nullptr) {
        lifecycle->close();
    }
}

bool BrokerConnectorComponent::onIncomingPublish(const Message& message,
                                                 const SourcePublishMeta& sourceMeta) {
    PublishCallback callback{};
    {
        std::lock_guard<std::mutex> lock{relay_state_mutex_};
        if (!running_ || !publishCallback_) {
            return false;
        }

        counters_.received += 1U;
        callback = publishCallback_;
    }

    const Message outgoingMessage = toForwardMessage(message, sourceMeta);

    std::uint32_t attempt = 0U;
    while (true) {
        try {
            callback(outgoingMessage);
            std::lock_guard<std::mutex> lock{relay_state_mutex_};
            counters_.forwarded += 1U;
            return true;
        } catch (const std::exception&) {
        } catch (...) {
        }

        if (attempt >= config_.maxPublishRetries) {
            std::lock_guard<std::mutex> lock{relay_state_mutex_};
            counters_.failed += 1U;
            return false;
        }

        if (config_.publishRetryBackoff.count() > 0) {
            std::this_thread::sleep_for(config_.publishRetryBackoff);
        }

        attempt += 1U;
    }

    std::lock_guard<std::mutex> lock{relay_state_mutex_};
    counters_.failed += 1U;
    return false;
}

RelayCounters BrokerConnectorComponent::getStats() const {
    std::lock_guard<std::mutex> lock{relay_state_mutex_};
    return counters_;
}

bool BrokerConnectorComponent::isRunning() const {
    std::lock_guard<std::mutex> lock{relay_state_mutex_};
    return running_;
}

Message BrokerConnectorComponent::toForwardMessage(const Message& message,
                                                   const SourcePublishMeta& sourceMeta) const {
    Qos targetQos = Qos::AtLeastOnce;
    if (config_.normalizeQosToAtLeastOnce) {
        targetQos = sourceMeta.qos == Qos::AtMostOnce ? Qos::AtMostOnce : Qos::AtLeastOnce;
    } else {
        targetQos = sourceMeta.qos;
    }

    const bool targetRetain = config_.retainPassthrough ? sourceMeta.retain : false;

    Message mapped{message.topic(), message.value(), targetQos, targetRetain};
    for (const auto& reasonEntry : message.reason()) {
        mapped.addReason(reasonEntry.message, reasonEntry.timestamp);
    }

    return mapped;
}

} // namespace yaha
