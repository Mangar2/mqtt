#pragma once

/**
 * @file rs485_topic_mapper.h
 * @brief MQTT <-> serial mapping helper for RS485 interface messages.
 */

#include "yaha/message/message.h"
#include "yaha/rs485_interface_client/rs485_interface_client_app.h"
#include "yaha/rs485_protocol/rs485_serial_protocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace yaha {

/**
 * @brief Normalized outbound serial data derived from one MQTT command message.
 */
struct Rs485MappedSerialData {
    std::uint8_t address{0U};
    char command{'\0'};
    std::uint16_t value{0U};
};

/**
 * @brief Maps between MQTT messages and RS485 serial messages.
 */
class Rs485TopicMapper {
public:
    /**
     * @brief Creates one mapper using parsed RS485 config.
     * @param config RS485 configuration data.
     */
    explicit Rs485TopicMapper(Rs485InterfaceConfig config);

    /**
     * @brief Maps one MQTT message to serial output data.
     * @param mqttMessage Input MQTT message.
     * @return Serial command tuple.
     * @throws std::runtime_error when mapping is impossible.
     */
    [[nodiscard]] Rs485MappedSerialData toSerialData(const Message& mqttMessage) const;

    /**
     * @brief Maps one serial message to one or more MQTT publish messages.
     * @param serialMessage Input serial frame.
     * @return MQTT messages to publish.
     * @throws std::runtime_error when mapping is impossible.
     */
    [[nodiscard]] std::vector<Message> toMqttMessages(const Rs485SerialMessage& serialMessage) const;

private:
    [[nodiscard]] std::uint8_t resolveAddressByTopic(const std::string& topic) const;
    [[nodiscard]] char resolveCommandByTopic(const std::string& topic) const;
    [[nodiscard]] std::uint16_t resolveValueByCommandAndPayload(
        char command,
        const Value& mqttValue) const;

    [[nodiscard]] std::string resolveTopicPrefixByAddress(std::uint8_t serialAddress) const;
    [[nodiscard]] std::string resolveTopicSuffixByCommand(char command) const;
    [[nodiscard]] Value resolveMqttValueByCommand(char command, double serialValue) const;

    Rs485InterfaceConfig config_{};
};

} // namespace yaha
