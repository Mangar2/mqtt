#include <catch2/catch_test_macros.hpp>

#include "yaha/serial_device/serial_device_contract.h"

namespace {

yaha::SerialDeviceConfig makeBaseConfig() {
    yaha::SerialDeviceConfig config{};
    config.subscribeQos = yaha::Qos::ExactlyOnce;
    return config;
}

} // namespace

TEST_CASE("derive_subscriptions_always_includes_system_topic", "[serial_device]") {
    const yaha::SerialDeviceConfig config = makeBaseConfig();

    const yaha::SubscriptionMap subscriptions = yaha::deriveSerialDeviceSubscriptions(config);

    REQUIRE(subscriptions.size() == 1U);
    REQUIRE(subscriptions.contains("$SYS/serialdevice/#"));
    REQUIRE(subscriptions.at("$SYS/serialdevice/#") == yaha::Qos::ExactlyOnce);
}

TEST_CASE("derive_subscriptions_adds_topic_map_topics_with_plus_suffix", "[serial_device]") {
    yaha::SerialDeviceConfig config = makeBaseConfig();

    yaha::SerialDeviceInterfaceDefinition switchInterface{};
    switchInterface.topicMap["house/light/switch"] = yaha::SerialDeviceSwitchTopicMapping{
        .command = "switch",
        .value = 1U,
        .address = "A1",
    };
    config.interfaces["switch"] = switchInterface;

    const yaha::SubscriptionMap subscriptions = yaha::deriveSerialDeviceSubscriptions(config);

    REQUIRE(subscriptions.contains("house/light/switch/+"));
    REQUIRE(subscriptions.at("house/light/switch/+") == yaha::Qos::ExactlyOnce);
}

TEST_CASE("derive_subscriptions_without_receiver_map_uses_command_suffix_only", "[serial_device]") {
    yaha::SerialDeviceConfig config = makeBaseConfig();

    yaha::SerialDeviceInterfaceDefinition fs20Interface{};
    fs20Interface.receiverMapProvided = false;
    fs20Interface.commandMap["on"] = "lights/livingroom/set";
    fs20Interface.commandMap["off"] = "lights/bedroom/set";
    config.interfaces["fs20"] = fs20Interface;

    const yaha::SubscriptionMap subscriptions = yaha::deriveSerialDeviceSubscriptions(config);

    REQUIRE(subscriptions.contains("lights/livingroom/set/+"));
    REQUIRE(subscriptions.contains("lights/bedroom/set/+"));
    REQUIRE(subscriptions.at("lights/livingroom/set/+") == yaha::Qos::ExactlyOnce);
}

TEST_CASE("derive_subscriptions_with_receiver_map_uses_prefix_and_suffix", "[serial_device]") {
    yaha::SerialDeviceConfig config = makeBaseConfig();

    yaha::SerialDeviceInterfaceDefinition i2cInterface{};
    i2cInterface.receiverMapProvided = true;
    i2cInterface.commandMap["temp"] = "sensor/read";
    i2cInterface.commandMap["hum"] = "sensor/humidity";
    i2cInterface.receiverMap["home/floor1/"] = "rx1";
    i2cInterface.receiverMap["home/floor2/"] = "rx2";
    config.interfaces["i2c"] = i2cInterface;

    const yaha::SubscriptionMap subscriptions = yaha::deriveSerialDeviceSubscriptions(config);

    REQUIRE(subscriptions.contains("home/floor1/sensor/read/+"));
    REQUIRE(subscriptions.contains("home/floor1/sensor/humidity/+"));
    REQUIRE(subscriptions.contains("home/floor2/sensor/read/+"));
    REQUIRE(subscriptions.contains("home/floor2/sensor/humidity/+"));
}

TEST_CASE("derive_subscriptions_with_empty_receiver_map_and_provided_flag_adds_no_command_topics", "[serial_device]") {
    yaha::SerialDeviceConfig config = makeBaseConfig();

    yaha::SerialDeviceInterfaceDefinition serialInterface{};
    serialInterface.receiverMapProvided = true;
    serialInterface.commandMap["set"] = "serial/device/set";
    config.interfaces["serial"] = serialInterface;

    const yaha::SubscriptionMap subscriptions = yaha::deriveSerialDeviceSubscriptions(config);

    REQUIRE(subscriptions.size() == 1U);
    REQUIRE(subscriptions.contains("$SYS/serialdevice/#"));
    REQUIRE_FALSE(subscriptions.contains("serial/device/set/+"));
}
