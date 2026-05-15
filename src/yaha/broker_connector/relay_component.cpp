#include "yaha/broker_connector/relay_component.h"

#include <exception>
#include <string_view>
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
    std::unique_ptr<SourceLifecycleManager> lifecycle{};
    {
        std::lock_guard<std::mutex> lock{relay_state_mutex_};
        running_ = false;
        lifecycle = std::move(sourceLifecycle_);
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
    constexpr std::string_view k_sys_topic_prefix{"$SYS/"};

    const std::string& sourceTopic = message.topic();
    std::string targetTopic = sourceTopic;
    if (sourceTopic == "$SYS") {
        targetTopic = "status";
    } else if (sourceTopic.starts_with(k_sys_topic_prefix)) {
        targetTopic = "status/" + sourceTopic.substr(k_sys_topic_prefix.size());
    }

    Qos targetQos = Qos::AtLeastOnce;
    if (config_.normalizeQosToAtLeastOnce) {
        targetQos = sourceMeta.qos == Qos::AtMostOnce ? Qos::AtMostOnce : Qos::AtLeastOnce;
    } else {
        targetQos = sourceMeta.qos;
    }

    const bool targetRetain = config_.retainPassthrough ? sourceMeta.retain : false;
    const bool targetDup = sourceMeta.dup && targetQos != Qos::AtMostOnce;

    Message mapped{targetTopic, message.value(), targetQos, targetRetain, targetDup};
    if (message.rawPayload().has_value()) {
        mapped.setRawPayload(*message.rawPayload());
    }
    for (const auto& reasonEntry : message.reason()) {
        mapped.addReason(reasonEntry.message, reasonEntry.timestamp);
    }

    return mapped;
}

} // namespace yaha
