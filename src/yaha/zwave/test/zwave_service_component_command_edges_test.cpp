#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

#include "yaha/zwave/zwave_service_component.h"

namespace {

constexpr std::uint16_t kNodeIdSeven{7U};
constexpr std::uint16_t kSwitchClass{0x25U};
constexpr double k_addnode_enable_value{1.0};
constexpr double k_addnode_disable_value{0.0};
constexpr double k_addnode_invalid_value{2.0};

class TestController final : public yaha::IZwaveController {
public:
    void setPublishCallback(yaha::PublishCallback callback) override {
        callbackFromService_ = std::move(callback);
    }

    void setDeviceConfiguration(const std::vector<yaha::ZwaveDeviceConfig>& devices) override {
        configuredDevices_ = devices;
    }

    void setValue(const std::string& topic,
                  const yaha::Value& value,
                  const std::vector<yaha::ReasonEntry>& reasons) override {
        (void)topic;
        (void)value;
        (void)reasons;
        setValueCalls_ += 1U;
    }

    void addDevice() override {
        addDeviceCalls_ += 1U;
    }

    void removeFailedNode(const yaha::Value& value) override {
        (void)value;
        removeFailedCalls_ += 1U;
    }

    void startScan() override {
        startScanCalls_ += 1U;
    }

    void requestConfigParametersForAllNodes() override {
        requestConfigCalls_ += 1U;
    }

    [[nodiscard]] std::vector<std::uint16_t> knownNodeIds() const override {
        if (throwOnKnownNodes_) {
            throw std::runtime_error{"known nodes failed"};
        }
        return knownNodeIds_;
    }

    void close() override {
        closeCalls_ += 1U;
    }

    void setThrowOnKnownNodes(const bool enabled) {
        throwOnKnownNodes_ = enabled;
    }

    void setKnownNodeIds(std::vector<std::uint16_t> nodeIds) {
        knownNodeIds_ = std::move(nodeIds);
    }

    void emitControllerPublish(const yaha::Message& message) const {
        if (callbackFromService_) {
            callbackFromService_(message);
        }
    }

    [[nodiscard]] std::size_t addDeviceCalls() const {
        return addDeviceCalls_;
    }

    [[nodiscard]] std::size_t requestConfigCalls() const {
        return requestConfigCalls_;
    }

private:
    yaha::PublishCallback callbackFromService_{};
    std::vector<yaha::ZwaveDeviceConfig> configuredDevices_{};
    std::vector<std::uint16_t> knownNodeIds_{};

    std::size_t setValueCalls_{0U};
    std::size_t addDeviceCalls_{0U};
    std::size_t removeFailedCalls_{0U};
    std::size_t startScanCalls_{0U};
    std::size_t requestConfigCalls_{0U};
    std::size_t closeCalls_{0U};

    bool throwOnKnownNodes_{false};
};

[[nodiscard]] yaha::ZwaveConfig makeConfig() {
    yaha::ZwaveConfig config{};
    config.subscribeQos = yaha::Qos::AtMostOnce;
    config.qos = yaha::Qos::ExactlyOnce;
    config.retain = true;
    config.usb.device = "/dev/ttyUSB0";
    config.usb.topic = "controller/topic";

    yaha::ZwaveDeviceConfig device{};
    device.topic = "ground/livingroom/zwave/switch/main";
    device.nodeId = kNodeIdSeven;
    device.classId = static_cast<std::uint16_t>(kSwitchClass);
    config.devices = {device};

    return config;
}

const yaha::Message* findLastMessageByTopic(const std::vector<yaha::Message>& messages, const std::string& topic) {
    for (const auto& message : std::views::reverse(messages)) {
        if (message.topic() == topic) {
            return &message;
        }
    }
    return nullptr;
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("zwave_addnode_numeric_and_unsupported_payload_paths", "[zwave_service]") {
    auto controller = std::make_shared<TestController>();
    yaha::ZwaveServiceComponent component{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    component.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    component.handleMessage(yaha::Message{"system/zwave/addnode/set", k_addnode_enable_value});
    REQUIRE(controller->addDeviceCalls() == 1U);

    component.handleMessage(yaha::Message{"system/zwave/addnode/set", k_addnode_disable_value});
    REQUIRE(controller->addDeviceCalls() == 1U);

    component.handleMessage(yaha::Message{"system/zwave/addnode/set", k_addnode_invalid_value});

    const yaha::Message* statusMessage = findLastMessageByTopic(published, "system/zwave/addnode");
    REQUIRE(statusMessage != nullptr);
    REQUIRE(std::holds_alternative<std::string>(statusMessage->value()));
    REQUIRE(std::get<std::string>(statusMessage->value()) == "off");

    const yaha::Message* errorMessage = findLastMessageByTopic(published, "system/zwave/error");
    REQUIRE(errorMessage != nullptr);
    REQUIRE(std::holds_alternative<std::string>(errorMessage->value()));
    REQUIRE(std::get<std::string>(errorMessage->value()) == "addnode failed");
}

TEST_CASE("zwave_controller_publish_keeps_raw_payload", "[zwave_service]") {
    auto controller = std::make_shared<TestController>();
    yaha::ZwaveServiceComponent component{makeConfig(), controller};

    std::vector<yaha::Message> published{};
    component.setPublishCallback([&published](const yaha::Message& message) {
        published.push_back(message.clone());
    });

    yaha::Message outgoing{"ground/livingroom/zwave/switch/main", std::string{"on"}};
    outgoing.setRawPayload("{\"raw\":true}");
    controller->emitControllerPublish(outgoing);

    REQUIRE_FALSE(published.empty());
    REQUIRE(published.back().rawPayload().has_value());
    REQUIRE(*published.back().rawPayload() == "{\"raw\":true}");
}

TEST_CASE("zwave_filestore_reload_handles_missing_and_failed_callback", "[zwave_service]") {
    auto config = makeConfig();
    config.fileStoreEnabled = true;
    config.fileStoreMonitorTopicPrefix = "$MONITOR/FileStore";
    config.settingsKeyPath = "/zwave/settings";

    auto controller = std::make_shared<TestController>();
    yaha::ZwaveServiceComponent component{config, controller};

    component.setPublishCallback([](const yaha::Message&) {
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/FileStore/changed",
        std::string{R"json({"keyPath":"/zwave/settings"})json"}});

    std::size_t reloadCalls{0U};
    component.setFileStoreReloadCallback([&reloadCalls](std::vector<yaha::ZwaveDeviceConfig>&, std::string& error) {
        reloadCalls += 1U;
        error = "reload failed";
        return false;
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/FileStore/changed",
        std::string{R"json({"keyPath":"/zwave/settings"})json"}});

    REQUIRE(reloadCalls == 1U);
}

TEST_CASE("zwave_filestore_reload_parses_escaped_keypath_and_ignores_invalid_escape", "[zwave_service]") {
    auto config = makeConfig();
    config.fileStoreEnabled = true;
    config.fileStoreMonitorTopicPrefix = "$MONITOR/FileStore";
    config.settingsKeyPath = "/zwave/settings";

    auto controller = std::make_shared<TestController>();
    yaha::ZwaveServiceComponent component{config, controller};
    component.setPublishCallback([](const yaha::Message&) {
    });

    std::size_t reloadCalls{0U};
    component.setFileStoreReloadCallback([&reloadCalls](std::vector<yaha::ZwaveDeviceConfig>& devices, std::string&) {
        reloadCalls += 1U;
        devices.clear();
        return true;
    });

    component.handleMessage(yaha::Message{
        "$MONITOR/FileStore/changed",
        std::string{R"json({"keyPath" : "\/zwave\/settings"})json"}});

    component.handleMessage(yaha::Message{
        "$MONITOR/FileStore/changed",
        std::string{R"json({"keyPath":"\q"})json"}});

    REQUIRE(reloadCalls == 1U);
}

TEST_CASE("zwave_run_continues_when_known_nodes_snapshot_throws", "[zwave_service]") {
    auto controller = std::make_shared<TestController>();
    controller->setThrowOnKnownNodes(true);

    yaha::ZwaveServiceComponent component{makeConfig(), controller};
    component.setPublishCallback([](const yaha::Message&) {
    });

    component.run();

    REQUIRE(controller->requestConfigCalls() == 1U);
}
