#include "yaha/serial_device/serial_device_contract.h"

namespace yaha {

namespace {

void deriveSubscriptionsFromTopicMap(
    const std::unordered_map<std::string, SerialDeviceSwitchTopicMapping>& topicMap,
    const Qos qos,
    SubscriptionMap& output) {
    for (const auto& topicEntry : topicMap) {
        output[topicEntry.first + "/+"] = qos;
    }
}

void deriveSubscriptionsFromCommandAndReceiverMap(
    const std::unordered_map<std::string, std::string>& commandMap,
    const std::unordered_map<std::string, std::string>& receiverMap,
    const bool receiverMapProvided,
    const Qos qos,
    SubscriptionMap& output) {
    if (receiverMapProvided) {
        for (const auto& receiverEntry : receiverMap) {
            for (const auto& commandEntry : commandMap) {
                output[receiverEntry.first + commandEntry.second + "/+"] = qos;
            }
        }
        return;
    }

    for (const auto& commandEntry : commandMap) {
        output[commandEntry.second + "/+"] = qos;
    }
}

} // namespace

SubscriptionMap deriveSerialDeviceSubscriptions(const SerialDeviceConfig& config) {
    SubscriptionMap subscriptions{};

    for (const auto& interfaceEntry : config.interfaces) {
        const SerialDeviceInterfaceDefinition& interfaceDefinition = interfaceEntry.second;
        deriveSubscriptionsFromTopicMap(interfaceDefinition.topicMap, config.subscribeQos, subscriptions);
        deriveSubscriptionsFromCommandAndReceiverMap(
            interfaceDefinition.commandMap,
            interfaceDefinition.receiverMap,
            interfaceDefinition.receiverMapProvided,
            config.subscribeQos,
            subscriptions);
    }

    subscriptions["$SYS/serialdevice/#"] = config.subscribeQos;
    return subscriptions;
}

} // namespace yaha
