#pragma once

/**
 * @file zwave_controller.h
 * @brief ZWave controller adapter contracts and parity routing behavior.
 */

#include "yaha/message/message.h"
#include "yaha/mqtt_component/mqtt_component.h"
#include "yaha/zwave/zwave_config.h"
#include "yaha/zwave_devices/zwave_devices_mapper.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace yaha {

inline constexpr std::uint16_t kZwaveSwitchBinaryClass = 0x25U;
inline constexpr std::uint16_t kZwaveSwitchMultilevelClass = 0x26U;

/**
 * @brief Notification code map from OpenZWave callback contract.
 */
enum class ZwaveNotificationCode : std::uint8_t {
    MessageComplete = 0U,
    Timeout = 1U,
    Nop = 2U,
    NodeAwake = 3U,
    NodeSleep = 4U,
    NodeDead = 5U,
    NodeAlive = 6U
};

/**
 * @brief One incoming value event payload from controller callbacks.
 */
struct ZwaveControllerValueEvent {
    std::uint16_t nodeId{0U};
    std::uint16_t classId{0U};
    std::uint8_t instance{1U};
    std::uint8_t index{0U};
    std::optional<std::string> label{};
    std::optional<std::uint64_t> valueId{};
    Value value{std::string{}};
    std::string type{"bool"};
    bool readOnly{false};
};

/**
 * @brief One node metadata object from driver callback.
 */
struct ZwaveNodeInfo {
    std::string manufacturer{};
    std::string manufacturerId{};
    std::string product{};
    std::string productType{};
    std::string productId{};
    std::string type{};
    std::string name{};
    std::string location{};
};

/**
 * @brief Abstract driver port used by controller adapter.
 */
class IZwaveDriverPort {
public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~IZwaveDriverPort() = default;

    /**
     * @brief Writes one regular value.
     * @param target Resolved ZWave id.
     * @param value Converted payload.
     */
    virtual void setValue(const ZwaveResolvedId& target, const std::variant<bool, double, std::string>& value) = 0;

    /**
     * @brief Writes one configuration parameter.
     * @param nodeId Target node id.
     * @param paramId Config parameter id.
     * @param value Numeric parameter value.
     */
    virtual void setConfigParam(std::uint16_t nodeId, std::uint16_t paramId, double value) = 0;

    /**
     * @brief Enables add-device flow on the controller.
     */
    virtual void addNode() = 0;

    /**
     * @brief Removes one failed node.
     * @param nodeId Node id.
     */
    virtual void removeFailedNode(std::uint16_t nodeId) = 0;

    /**
     * @brief Starts scan/inclusion flow.
     */
    virtual void startScan() = 0;

    /**
     * @brief Requests all config params for one node.
     * @param nodeId Node id.
     */
    virtual void requestAllConfigParams(std::uint16_t nodeId) = 0;

    /**
     * @brief Enables polling for one class.
     * @param nodeId Node id.
     * @param classId Command class id.
     */
    virtual void enablePoll(std::uint16_t nodeId, std::uint16_t classId) = 0;

    /**
     * @brief Disconnects from configured USB device.
     * @param devicePath USB device path.
     */
    virtual void disconnect(const std::string& devicePath) = 0;
};

/**
 * @brief Abstract controller port used by ZWave service.
 */
class IZwaveController {
public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~IZwaveController() = default;

    /**
     * @brief Sets publish callback used by controller outputs.
     * @param callback Publish callback.
     */
    virtual void setPublishCallback(PublishCallback callback) = 0;

    /**
     * @brief Updates mapping configuration used by controller.
     * @param devices Device mapping rows.
     */
    virtual void setDeviceConfiguration(const std::vector<ZwaveDeviceConfig>& devices) = 0;

    /**
     * @brief Routes one incoming MQTT set message by topic.
     * @param topic Incoming `/set` topic.
     * @param value Incoming payload.
     */
    virtual void setValue(const std::string& topic, const Value& value) = 0;

    /**
     * @brief Starts add-node operation.
     */
    virtual void addDevice() = 0;

    /**
     * @brief Starts remove-failed-node operation.
     * @param value Incoming payload containing node id.
     */
    virtual void removeFailedNode(const Value& value) = 0;

    /**
     * @brief Starts controller scan operation.
     */
    virtual void startScan() = 0;

    /**
     * @brief Requests config parameters for all configured nodes.
     */
    virtual void requestConfigParametersForAllNodes() = 0;

    /**
     * @brief Disconnects controller.
     */
    virtual void close() = 0;
};

/**
 * @brief Concrete parity controller adapter used by ZWave service.
 */
class ZwaveController final : public IZwaveController {
public:
    /**
     * @brief Constructs controller adapter.
     * @param usbConfig Controller USB configuration.
     * @param driverPort Low-level driver port.
     */
    ZwaveController(ZwaveUsbConfig usbConfig, IZwaveDriverPort& driverPort);

    /**
     * @brief Sets publish callback used by controller outputs.
     * @param callback Publish callback.
     */
    void setPublishCallback(PublishCallback callback) override;

    /**
     * @brief Updates mapping configuration used by controller.
     * @param devices Device mapping rows.
     */
    void setDeviceConfiguration(const std::vector<ZwaveDeviceConfig>& devices) override;

    /**
     * @brief Routes one incoming MQTT set message by topic.
     * @param topic Incoming `/set` topic.
     * @param value Incoming payload.
     */
    void setValue(const std::string& topic, const Value& value) override;

    /**
     * @brief Starts add-node operation.
     */
    void addDevice() override;

    /**
     * @brief Starts remove-failed-node operation.
     * @param value Incoming payload containing node id.
     */
    void removeFailedNode(const Value& value) override;

    /**
     * @brief Starts controller scan operation.
     */
    void startScan() override;

    /**
     * @brief Requests config parameters for all configured nodes.
     */
    void requestConfigParametersForAllNodes() override;

    /**
     * @brief Disconnects controller.
     */
    void close() override;

    /**
     * @brief Handles driver-ready callback and publishes start-scan notification.
     * @param homeId Home id from driver callback.
     */
    void onDriverReady(std::uint32_t homeId);

    /**
     * @brief Handles driver-failed callback.
     */
    void onDriverFailed();

    /**
     * @brief Handles scan-complete callback.
     */
    void onScanComplete();

    /**
     * @brief Handles generic notification callback.
     * @param nodeId Node id.
     * @param notification Notification code.
     */
    void onNotification(std::uint16_t nodeId, ZwaveNotificationCode notification);

    /**
     * @brief Handles controller command feedback callback.
     * @param resultCode Numeric result code.
     * @param statusText Controller status text.
     */
    void onControllerCommand(std::int32_t resultCode, const std::string& statusText);

    /**
     * @brief Handles node-added callback.
     * @param nodeId Node id.
     */
    void onNodeAdded(std::uint16_t nodeId);

    /**
     * @brief Handles node-ready callback.
     * @param nodeId Node id.
     * @param nodeInfo Node metadata.
     */
    void onNodeReady(std::uint16_t nodeId, const ZwaveNodeInfo& nodeInfo);

    /**
     * @brief Handles value-added callback.
     * @param event Value event payload.
     */
    void onValueAdded(const ZwaveControllerValueEvent& event);

    /**
     * @brief Handles value-removed callback.
     * @param nodeId Node id.
     * @param classId Command class id.
     * @param index Value index.
     */
    void onValueRemoved(std::uint16_t nodeId, std::uint16_t classId, std::uint8_t index);

    /**
     * @brief Handles value-changed callback and publishes mapped message.
     * @param event Value event payload.
     */
    void onValueChanged(const ZwaveControllerValueEvent& event);

    /**
     * @brief Handles value-refreshed callback.
     * @param nodeId Node id.
     * @param classId Command class id.
     * @param event Value event payload.
     */
    static void onValueRefreshed(
        std::uint16_t nodeId,
        std::uint16_t classId,
        const ZwaveControllerValueEvent& event);

private:
    struct NodeRuntimeState {
        ZwaveNodeInfo info{};
        bool ready{false};
        std::unordered_map<std::uint16_t, std::unordered_map<std::uint8_t, ZwaveControllerValueEvent>> classes{};
    };

    [[nodiscard]] static std::optional<std::uint16_t> parseNodeIdFromValue(const Value& value);
    [[nodiscard]] static std::optional<std::string> parseOptionalLabelFromSetTopic(const std::vector<std::string>& topicParts);
    [[nodiscard]] static std::string joinTopicParts(const std::vector<std::string>& parts, std::size_t count);
    [[nodiscard]] static std::vector<std::string> splitTopic(const std::string& topic);
    [[nodiscard]] ZwaveNodeMap buildNodeMap() const;
    [[nodiscard]] static ZwaveValueDescriptor buildDescriptor(const ZwaveControllerValueEvent& event);
    [[nodiscard]] static std::string notificationText(ZwaveNotificationCode notification);

    void publish(const std::string& topic, const Value& value, const std::string& reason);
    void publishValue(std::uint16_t nodeId, const ZwaveControllerValueEvent& event, std::string reason);
    void storeNodeValue(const ZwaveControllerValueEvent& event);

    ZwaveUsbConfig usb_{};
    IZwaveDriverPort& driverPort_;

    std::vector<ZwaveDeviceConfig> devices_{};
    ZwaveDevicesMapper devicesMapper_{std::vector<ZwaveDeviceConfig>{}};
    std::unordered_map<std::uint16_t, NodeRuntimeState> nodes_{};
    PublishCallback publishCallback_{};
};

} // namespace yaha
