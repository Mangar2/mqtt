#include "yaha/broker_connector/receiver_publish_port.h"

#include "yaha/mqtt_client/broker_transport.h"

#include "yaha/mqtt_component/mqtt_component.h"

#include <exception>
#include <utility>

namespace yaha {

ReceiverPublishPort::~ReceiverPublishPort() = default;

namespace {

class ReceiverSinkComponent final : public IMqttComponent {
public:
    [[nodiscard]] SubscriptionMap getSubscriptions() const override {
        return {};
    }

    void handleMessage(const Message& message) override {
        (void)message;
    }

    void run() override {
        running_ = true;
    }

    void close() override {
        running_ = false;
    }

private:
    bool running_{false};
};

} // namespace

struct ReceiverMqttPublishPort::Impl {
    std::unique_ptr<ReceiverSinkComponent> sinkComponent{};
    std::unique_ptr<YahaMqttClient> mqttClient{};
};

ReceiverMqttPublishPort::ReceiverMqttPublishPort(ReceiverMqttBrokerConfig config)
    : ReceiverMqttPublishPort(std::move(config), makeBrokerTransport()) {}

ReceiverMqttPublishPort::ReceiverMqttPublishPort(ReceiverMqttBrokerConfig config,
                                                 YahaMqttClient::Transport transport)
    : config_(std::move(config))
    , transport_(std::move(transport)) {}

ReceiverMqttPublishPort::~ReceiverMqttPublishPort() {
    close();
}

bool ReceiverMqttPublishPort::start(std::string& errorMessage) {
    std::lock_guard<std::mutex> lock{runtime_state_mutex_};
    if (impl_ != nullptr && impl_->mqttClient != nullptr && impl_->mqttClient->isRunning()) {
        return true;
    }

    try {
        auto implementation = std::make_unique<Impl>();
        implementation->sinkComponent = std::make_unique<ReceiverSinkComponent>();
        implementation->mqttClient = std::make_unique<YahaMqttClient>(
            toClientConfig(config_), *implementation->sinkComponent, transport_);

        implementation->sinkComponent->run();
        implementation->mqttClient->run();

        impl_ = std::move(implementation);
        return true;
    } catch (const std::exception& exceptionValue) {
        errorMessage = std::string{"receiver publish start failed: "} + exceptionValue.what();
    } catch (...) {
        errorMessage = "receiver publish start failed: unknown error";
    }

    impl_.reset();
    return false;
}

void ReceiverMqttPublishPort::close() {
    std::lock_guard<std::mutex> lock{runtime_state_mutex_};
    if (impl_ == nullptr) {
        return;
    }

    if (impl_->mqttClient != nullptr) {
        impl_->mqttClient->close();
    }
    if (impl_->sinkComponent != nullptr) {
        impl_->sinkComponent->close();
    }

    impl_.reset();
}

bool ReceiverMqttPublishPort::publish(const Message& message,
                                      const ReceiverPublishOptions& options,
                                      std::string& errorMessage) {
    std::lock_guard<std::mutex> lock{runtime_state_mutex_};
    if (impl_ == nullptr || impl_->mqttClient == nullptr || !impl_->mqttClient->isRunning()) {
        errorMessage = "receiver publish runtime not started";
        return false;
    }

    try {
        impl_->mqttClient->publish(applyPublishOptions(message, options));
        return true;
    } catch (const std::exception& exceptionValue) {
        errorMessage = std::string{"receiver publish failed: "} + exceptionValue.what();
    } catch (...) {
        errorMessage = "receiver publish failed: unknown error";
    }

    return false;
}

bool ReceiverMqttPublishPort::isConnected() const {
    std::lock_guard<std::mutex> lock{runtime_state_mutex_};
    if (impl_ == nullptr || impl_->mqttClient == nullptr) {
        return false;
    }

    return impl_->mqttClient->isConnected();
}

YahaMqttClient::Config
ReceiverMqttPublishPort::toClientConfig(const ReceiverMqttBrokerConfig& config) {
    YahaMqttClient::Config result{};
    result.brokerHost = config.brokerHost;
    result.brokerPort = config.brokerPort;
    result.clientId = config.clientId;
    result.reconnectDelay = config.reconnectDelay;
    result.keepAliveInterval = config.keepAliveInterval;
    result.loopSleep = config.loopSleep;
    result.enableLifecycleTrace = config.enableLifecycleTrace;
    result.enableMessageTrace = config.enableMessageTrace;
    return result;
}

Message ReceiverMqttPublishPort::applyPublishOptions(const Message& message,
                                                     const ReceiverPublishOptions& options) {
    const bool targetDup = options.dup && options.qos != Qos::AtMostOnce;
    Message mapped{message.topic(), message.value(), options.qos, options.retain, targetDup};
    if (message.rawPayload().has_value()) {
        mapped.setRawPayload(*message.rawPayload());
    }
    for (const auto& reasonEntry : message.reason()) {
        mapped.addReason(reasonEntry.message, reasonEntry.timestamp);
    }

    return mapped;
}

} // namespace yaha
