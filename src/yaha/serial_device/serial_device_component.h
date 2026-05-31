#pragma once

/**
 * @file serial_device_component.h
 * @brief SerialDevice IMqttComponent runtime with serial lifecycle, queue, and keep-alive behavior.
 */

#include "yaha/message/message.h"
#include "yaha/mqtt_component/mqtt_component.h"
#include "yaha/serial_device/serial_device_contract.h"
#include "yaha/serial_device/serial_device_parser.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace yaha {

/**
 * @brief Serial transport boundary used by SerialDevice runtime.
 */
class ISerialDeviceTransport {
public:
    using ReceiveCallback = std::function<void(const std::vector<std::uint8_t>&)>;

    /**
     * @brief Virtual destructor.
     */
    virtual ~ISerialDeviceTransport();

    /**
     * @brief Opens serial transport.
     * @param portName Port path/name.
     * @param baudrate Baudrate.
     */
    virtual void open(const std::string& portName, std::uint32_t baudrate) = 0;

    /**
     * @brief Closes serial transport.
     */
    virtual void close() = 0;

    /**
     * @brief Sends one text payload to serial transport.
     * @param payloadText Payload text.
     */
    virtual void sendData(const std::string& payloadText) = 0;

    /**
     * @brief Requests transport to print/list available serial ports.
     */
    virtual void listAvailablePorts() = 0;

    /**
     * @brief Sets receive callback for incoming serial bytes.
     * @param callbackValue Callback to invoke.
     */
    virtual void setReceiveCallback(ReceiveCallback callbackValue) = 0;

    /**
     * @brief Reports current transport open state.
     * @return True when transport is open.
     */
    [[nodiscard]] virtual bool isOpen() const = 0;
};

/**
 * @brief Runtime SerialDevice component with legacy-compatible lifecycle behavior.
 */
class SerialDeviceComponent final : public IMqttComponent {
public:
    using DelayFunction = std::function<void(std::chrono::milliseconds)>;

    /**
     * @brief Constructs component.
     * @param configValue Runtime/domain config.
     * @param transportValue Serial transport boundary.
     * @param delayFunction Delay callback used for keep-alive/retry/queue pacing.
     */
    SerialDeviceComponent(
        SerialDeviceConfig configValue,
        std::shared_ptr<ISerialDeviceTransport> transportValue,
        DelayFunction delayFunction = {});

    /**
     * @brief Destructor that closes runtime threads/transport.
     */
    ~SerialDeviceComponent() override;

    /**
     * @brief Returns subscriptions required by SerialDevice behavior.
     * @return Topic filter map.
     */
    [[nodiscard]] SubscriptionMap getSubscriptions() const override;

    /**
     * @brief Handles incoming MQTT message.
     * @param messageValue Incoming MQTT message.
     */
    void handleMessage(const Message& messageValue) override;

    /**
     * @brief Starts runtime loops and serial interface lifecycle.
     */
    void run() override;

    /**
     * @brief Stops runtime loops and closes serial interface.
     */
    void close() override;

    /**
     * @brief Sets outgoing publish callback.
     * @param callbackValue Callback to publish mapped MQTT messages.
     */
    void setPublishCallback(PublishCallback callbackValue) override;

    /**
     * @brief Returns current trace level for diagnostics/tests.
     * @return Current trace-level string.
     */
    [[nodiscard]] std::string traceLevel() const;

private:
    struct MatchedRequest {
        std::string topic{};
        Value value{};
        ReasonList reason{};
    };

    [[nodiscard]] static bool topicsMatchIgnoringSetSuffix(
        const std::string& leftTopic,
        const std::string& rightTopic);
    [[nodiscard]] static std::string stripSetSuffix(const std::string& topicValue);
    [[nodiscard]] static std::string stripActionSuffix(const std::string& topicValue);
    [[nodiscard]] static bool isActionTopic(const std::string& topicValue);
    [[nodiscard]] static std::string valueToText(const Value& valueValue);

    void runSendKeepAliveLoop();
    void runSendQueueLoop();

    void processReceivedSerialData(const std::vector<std::uint8_t>& chunkBytes);
    void processParsedSerialMessage(const SerialDeviceMessage& serialMessage);

    void publishMessages(const std::vector<Message>& mqttMessages);

    void enqueueSendPayload(const std::string& payloadText);
    [[nodiscard]] bool tryDequeueSendPayload(std::string& payloadText);

    void sendDataToSerialWithRetry(const std::string& serialString);
    void openSerialInterface();
    void delayFor(std::chrono::milliseconds durationValue) const;

    [[nodiscard]] bool hasMatchingMessage(const Message& messageValue) const;
    [[nodiscard]] Message matchAndUpdateReplyMessage(const Message& messageValue);
    void addReceivedMessage(const Message& messageValue);
    void logIncomingMessageIfEnabled(const Message& messageValue) const;
    void logOutgoingMessageIfEnabled(const Message& messageValue) const;
    static void logOutgoingFailure(const Message& messageValue,
                                   const std::string& categoryText,
                                   const std::string& reasonText);

    SerialDeviceConfig config_{};
    std::shared_ptr<ISerialDeviceTransport> transport_{};
    DelayFunction delayFunction_{};

    mutable std::mutex publishMutex_{};
    PublishCallback publishCallback_{};

    mutable std::mutex matcherMutex_{};
    std::deque<MatchedRequest> receivedMessages_{};

    mutable std::mutex stateMutex_{};
    bool isClosed_{true};
    bool isRunning_{false};
    bool isSerialInterfaceOpen_{false};

    mutable std::mutex queueMutex_{};
    std::condition_variable queueCondition_{};
    std::deque<std::string> sendQueue_{};

    std::thread keepAliveThread_{};
    std::thread sendQueueThread_{};

    SerialDeviceStreamParser parser_{};
};

} // namespace yaha
