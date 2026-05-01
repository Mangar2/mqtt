#include "yaha/broker_connector/relay_component.h"

#include <thread>

namespace yaha {

BrokerConnectorComponent::BrokerConnectorComponent(RelayPolicyConfig config)
    : config_(config) {}

BrokerConnectorComponent::~BrokerConnectorComponent() {
    close();
}

void BrokerConnectorComponent::setReceiverPublishPort(ReceiverPublishPort& receiverPort) {
    std::lock_guard<std::mutex> lock{relay_state_mutex_};
    receiverPort_ = &receiverPort;
}

void BrokerConnectorComponent::run() {
    std::lock_guard<std::mutex> lock{relay_state_mutex_};
    running_ = true;
}

void BrokerConnectorComponent::close() {
    std::lock_guard<std::mutex> lock{relay_state_mutex_};
    running_ = false;
}

bool BrokerConnectorComponent::onIncomingPublish(const Message& message,
                                                 const SourcePublishMeta& sourceMeta) {
    ReceiverPublishPort* receiverPort = nullptr;
    {
        std::lock_guard<std::mutex> lock{relay_state_mutex_};
        if (!running_ || receiverPort_ == nullptr) {
            return false;
        }

        counters_.received += 1U;
        receiverPort = receiverPort_;
    }

    const ReceiverPublishOptions publishOptions = toPublishOptions(sourceMeta);

    std::uint32_t attempt = 0U;
    while (attempt <= config_.maxPublishRetries) {
        std::string errorMessage{};
        if (receiverPort->publish(message, publishOptions, errorMessage)) {
            std::lock_guard<std::mutex> lock{relay_state_mutex_};
            counters_.forwarded += 1U;
            return true;
        }

        if (attempt == config_.maxPublishRetries) {
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

ReceiverPublishOptions
BrokerConnectorComponent::toPublishOptions(const SourcePublishMeta& sourceMeta) const {
    ReceiverPublishOptions options{};
    if (config_.normalizeQosToAtLeastOnce) {
        options.qos = sourceMeta.qos == Qos::AtMostOnce ? Qos::AtMostOnce : Qos::AtLeastOnce;
    } else {
        options.qos = sourceMeta.qos;
    }

    options.retain = config_.retainPassthrough ? sourceMeta.retain : false;
    options.dup = sourceMeta.dup;
    return options;
}

} // namespace yaha
