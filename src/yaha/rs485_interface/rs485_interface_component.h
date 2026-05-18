#pragma once

/**
 * @file rs485_interface_component.h
 * @brief RS485 IMqttComponent boundary implementation.
 */

#include "yaha/message/message.h"
#include "yaha/mqtt_component/mqtt_component.h"
#include "yaha/rs485_interface/rs485_topic_mapper.h"
#include "yaha/rs485_interface_client/rs485_interface_client_app.h"
#include "yaha/rs485_protocol/rs485_serial_protocol.h"
#include "yaha/rs485_state/rs485_scheduler.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace yaha {

/**
 * @brief RS485 domain component implementing MQTT boundary contract.
 */
class Rs485InterfaceComponent final : public IMqttComponent {
public:
    using SerialSendCallback = std::function<void(const std::vector<std::uint8_t>&)>;

    explicit Rs485InterfaceComponent(Rs485InterfaceConfig config);
    ~Rs485InterfaceComponent() override;

    [[nodiscard]] SubscriptionMap getSubscriptions() const override;
    void handleMessage(const Message& message) override;
    void run() override;
    void close() override;
    void setPublishCallback(PublishCallback callback) override;

    /**
     * @brief Sets callback invoked for outbound encoded serial bytes.
     * @param callback Serial send callback.
     */
    void setSerialSendCallback(SerialSendCallback callback);

    /**
     * @brief Feeds received serial bytes into decode and publish pipeline.
     * @param byteChunk Raw serial bytes.
     */
    void feedSerialBytes(const std::vector<std::uint8_t>& byteChunk);

private:
    [[nodiscard]] static std::string toLowerCopy(std::string text);
    [[nodiscard]] static bool endsWith(const std::string& text, const std::string& suffix);
    [[nodiscard]] static std::string removeSuffix(const std::string& text, const std::string& suffix);

    [[nodiscard]] static std::string deriveWildcardStartTopic(const std::string& addressTopic);
    [[nodiscard]] static std::optional<std::uint32_t> parsePositiveInteger(const Value& value);

    void processActionMessage(const Message& message);
    void enqueueSet(const std::string& topic, const Value& value);
    void enqueueTemporary(const std::string& topic, const Value& value);
    void enqueueBlink(const std::string& topic, const Value& value);

    void launchActionThread(std::function<void()> job);
    void runSchedulerLoop();
    void runTimeOfDayLoop();

    void onSchedulerSend(const Rs485SerialMessage& message);
    void publishMappedMessages(const std::vector<Message>& messages);

    [[nodiscard]] std::string readCachedTopicState(const std::string& topic) const;
    void updateTopicStateCache(const Message& message);

    Rs485InterfaceConfig config_{};
    Rs485TopicMapper mapper_;
    Rs485Scheduler scheduler_;

    mutable std::mutex publishMutex_{};
    PublishCallback publishCallback_{};

    mutable std::mutex serialSendMutex_{};
    SerialSendCallback serialSendCallback_{};

    mutable std::mutex topicStateMutex_{};
    std::map<std::string, std::string> topicStateCache_{};

    std::atomic<bool> running_{false};
    std::thread schedulerThread_{};
    std::thread timeOfDayThread_{};

    mutable std::mutex actionThreadsMutex_{};
    std::vector<std::thread> actionThreads_{};
};

} // namespace yaha
