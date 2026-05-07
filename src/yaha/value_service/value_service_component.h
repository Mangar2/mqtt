#pragma once

/**
 * @file value_service_component.h
 * @brief ValueService component config and IMqttComponent contract.
 */

#include "yaha/message/message.h"
#include "yaha/mqtt_component/mqtt_component.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace yaha {

inline constexpr std::uint16_t kDefaultValueServiceFileStorePort = 8210U;

/**
 * @brief Runtime configuration for ValueService component.
 */
struct ValueServiceConfig {
    std::string monitorTopicPrefix{"$MONITOR/FileStore"}; ///< Monitoring topic prefix for FileStore events.
    std::string valuesKeyPath{"/valueservice/values"};     ///< FileStore key path for full value map.
    std::string fileStoreHost{"127.0.0.1"};               ///< FileStore HTTP host.
    std::uint16_t fileStorePort{kDefaultValueServiceFileStorePort}; ///< FileStore HTTP port.
    bool fileStoreEnabled{true};                            ///< Enables FileStore load/save behavior.
    Qos subscribeQos{Qos::AtLeastOnce};                     ///< QoS for subscriptions and outbound state publishes.
    std::string legacyValuesFileName{};                     ///< Legacy migration key only, runtime-local file IO is disabled.
};

/**
 * @brief YAHA ValueService component with FileStore-backed value map lifecycle.
 */
class ValueServiceComponent final : public IMqttComponent {
public:
    /**
     * @brief Constructs component from runtime configuration.
     * @param config Runtime configuration.
     */
    explicit ValueServiceComponent(ValueServiceConfig config);

    /**
     * @brief Returns subscriptions for monitor channel and known `/set` topics.
     * @return Topic filter map.
     */
    [[nodiscard]] SubscriptionMap getSubscriptions() const override;

    /**
     * @brief Handles one incoming MQTT message.
     * @param message Incoming message.
     */
    void handleMessage(const Message& message) override;

    /**
     * @brief Starts lifecycle and optional startup load from FileStore.
     */
    void run() override;

    /**
     * @brief Stops lifecycle.
     */
    void close() override;

    /**
     * @brief Stores publish callback used for retained value outputs.
     * @param callback Publish callback.
     */
    void setPublishCallback(PublishCallback callback) override;

    /**
     * @brief Returns whether lifecycle is currently running.
     * @return True when running.
     */
    [[nodiscard]] bool isRunning() const;

    /**
     * @brief Returns current number of in-memory values.
     * @return Number of key/value entries.
     */
    [[nodiscard]] std::size_t valueCount() const;

    /**
     * @brief Returns one current value by key for tests/diagnostics.
     * @param key Topic key without `/set` suffix.
     * @return Value when found, otherwise nullopt.
     */
    [[nodiscard]] std::optional<Value> valueForKey(const std::string& key) const;

private:
    using ValueMap = std::map<std::string, Value>;

    [[nodiscard]] bool loadValuesFromFileStore();
    [[nodiscard]] bool persistValuesToFileStore() const;

    void handleMonitoringMessage(const Message& message);
    void handleSetMessage(const Message& message);

    [[nodiscard]] bool isMonitoringTopic(const std::string& topicName) const;
    [[nodiscard]] static bool isSetTopic(const std::string& topicName);
    [[nodiscard]] static std::string stripSetSuffix(const std::string& topicName);
    [[nodiscard]] static std::optional<std::string> extractJsonStringField(
        const std::string& payload,
        const std::string& fieldName);

    [[nodiscard]] static bool isSupportedValueType(const Value& value);
    [[nodiscard]] static std::string serializeValueMap(const ValueMap& values);
    [[nodiscard]] static bool parseValueMapJson(const std::string& jsonText, ValueMap& output);

    void publishRetainedValue(const std::string& key, const Value& value) const;
    void publishAllValuesSnapshot() const;

    ValueServiceConfig config_{};

    mutable std::mutex stateMutex_;
    ValueMap values_;
    bool running_{false};

    mutable std::mutex publishMutex_;
    PublishCallback publishCallback_;
};

} // namespace yaha
