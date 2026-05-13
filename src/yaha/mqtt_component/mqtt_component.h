#pragma once

/**
 * @file mqtt_component.h
 * @brief IMqttComponent interface for YAHA MQTT-facing components.
 */

#include "yaha/message/message.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <type_traits>
#include <utility>

namespace yaha {

/**
 * @brief Mapping of MQTT topic filters to requested QoS level.
 */
using SubscriptionMap = std::map<std::string, Qos>;

/**
 * @brief Publish failure category for outgoing component->mqtt sends.
 */
enum class PublishFailureCategory : std::uint8_t {
    None = 0U,
    Disconnected = 1U,
    AckTimeout = 2U,
    WriteFailed = 3U,
    CallbackMissing = 4U,
    Unknown = 5U,
};

/**
 * @brief Delivery result returned by publish callback invocations.
 */
struct PublishResult {
    bool success{true};                                  ///< True when message delivery is confirmed.
    PublishFailureCategory category{PublishFailureCategory::None}; ///< Failure category when success is false.
    std::string reason{};                               ///< Optional failure detail text.

    /**
     * @brief Returns success result.
     * @return Success publish result.
     */
    [[nodiscard]] static PublishResult ok();

    /**
     * @brief Returns failure result.
     * @param categoryValue Failure category.
     * @param reasonText Optional reason text.
     * @return Failed publish result.
     */
    [[nodiscard]] static PublishResult fail(PublishFailureCategory categoryValue,
                                            std::string reasonText = {});
};

/**
 * @brief Callback wrapper for outgoing publishes from a component.
 *
 * Wraps both legacy `void(const Message&)` and result-aware
 * `PublishResult(const Message&)` callables.
 */
class PublishCallback {
public:
    PublishCallback() = default;

    /**
     * @brief Constructs from callable.
     * @tparam CallableT Callable type.
     * @param callableValue Callable to wrap.
     */
    template <typename CallableT,
              typename = std::enable_if_t<!std::is_same_v<std::decay_t<CallableT>, PublishCallback>>>
    PublishCallback(CallableT&& callableValue) {
        assign(std::forward<CallableT>(callableValue));
    }

    /**
     * @brief Assigns from callable.
     * @tparam CallableT Callable type.
     * @param callableValue Callable to wrap.
     * @return Reference to this callback.
     */
    template <typename CallableT,
              typename = std::enable_if_t<!std::is_same_v<std::decay_t<CallableT>, PublishCallback>>>
    PublishCallback& operator=(CallableT&& callableValue) {
        assign(std::forward<CallableT>(callableValue));
        return *this;
    }

    /**
     * @brief Returns true when callable is set.
     * @return True when callback is callable.
     */
    [[nodiscard]] explicit operator bool() const {
        return static_cast<bool>(callback_);
    }

    /**
     * @brief Invokes wrapped callback.
     * @param message Message to publish.
     * @return Delivery result.
     */
    PublishResult operator()(const Message& message) const {
        if (!callback_) {
            return PublishResult::fail(PublishFailureCategory::CallbackMissing,
                                       "callback_missing");
        }
        return callback_(message);
    }

private:
    template <typename CallableT>
    void assign(CallableT&& callableValue) {
        using ResultT = std::invoke_result_t<CallableT, const Message&>;
        if constexpr (std::is_same_v<ResultT, void>) {
            callback_ = [innerCallable = std::forward<CallableT>(callableValue)](const Message& message) mutable {
                innerCallable(message);
                return PublishResult::ok();
            };
        } else {
            callback_ = std::forward<CallableT>(callableValue);
        }
    }

    std::function<PublishResult(const Message&)> callback_{};
};

/**
 * @brief Abstract boundary between a YAHA component and MQTT transport.
 */
class IMqttComponent {
public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~IMqttComponent();

    /**
     * @brief Returns topic filters this component wants to subscribe to.
     * @return Topic-filter to QoS map.
     */
    [[nodiscard]] virtual SubscriptionMap getSubscriptions() const = 0;

    /**
     * @brief Handles one incoming message delivered by the MQTT client.
     * @param message Incoming message.
     */
    virtual void handleMessage(const Message& message) = 0;

    /**
     * @brief Starts component lifecycle.
     */
    virtual void run() = 0;

    /**
     * @brief Stops component lifecycle.
     */
    virtual void close() = 0;

    /**
     * @brief Injects publish callback used for outgoing messages.
     *
     * Components that do not publish may ignore this callback.
     *
     * @param callback Publish callback to broker transport.
     */
    virtual void setPublishCallback(PublishCallback callback);

protected:
    /**
     * @brief Protected default constructor.
     */
    IMqttComponent() = default;

    /**
     * @brief Copy constructor.
     * @param other Source object.
     */
    IMqttComponent(const IMqttComponent& other) = default;

    /**
     * @brief Copy assignment operator.
     * @param other Source object.
     * @return Reference to this object.
     */
    IMqttComponent& operator=(const IMqttComponent& other) = default;

    /**
     * @brief Move constructor.
     * @param other Source object.
     */
    IMqttComponent(IMqttComponent&& other) = default;

    /**
     * @brief Move assignment operator.
     * @param other Source object.
     * @return Reference to this object.
     */
    IMqttComponent& operator=(IMqttComponent&& other) = default;
};

} // namespace yaha
