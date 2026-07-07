#pragma once

/**
 * @file openzwave_runtime_driver_port.h
 * @brief OpenZWave runtime-backed implementation of the ZWave driver port.
 */

#include "yaha/zwave_controller/zwave_controller.h"
#include "yaha/zwave_client/openzwave_notification_bridge.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace OpenZWave {
class Notification;
} // namespace OpenZWave

namespace yaha {

/**
 * @brief Runtime OpenZWave implementation for IZwaveDriverPort.
 */
class OpenZwaveRuntimeDriverPort final : public IZwaveDriverPort {
public:
    /**
     * @brief Constructs runtime driver port.
     * @param controllerPath Serial or transport path to the USB controller.
      * @param logLevel Unified logging level (0..4).
      * @param pollIntervalMs OpenZWave poll interval in milliseconds.
     */
     explicit OpenZwaveRuntimeDriverPort(
          std::string controllerPath,
          std::uint8_t logLevel,
        std::int64_t pollIntervalMs);

    /**
     * @brief Cleans up OpenZWave watcher, driver and manager ownership.
     */
    ~OpenZwaveRuntimeDriverPort() override;

    /**
     * @brief Initializes OpenZWave runtime and opens the configured controller.
     */
    void start();

    /**
     * @brief Binds callback sink used for translated controller events.
     * @param controller Controller adapter sink.
     */
    void bindController(ZwaveController& controller);

    /**
     * @brief Writes one regular value.
     * @param target Resolved ZWave id.
     * @param value Converted payload.
        * @note Canonical single write entry: target resolution is based on
        *       node/class/instance/index metadata from mapping, not cached raw ValueID ids.
     */
    void setValue(const ZwaveResolvedId& target, const std::variant<bool, double, std::string>& value) override;

    /**
     * @brief Writes one configuration parameter.
     * @param nodeId Target node id.
     * @param paramId Parameter index.
     * @param value Numeric payload.
     */
    void setConfigParam(std::uint16_t nodeId, std::uint16_t paramId, double value) override;

    /**
     * @brief Starts add-node flow on the controller.
     */
    void addNode() override;

    /**
     * @brief Removes one failed node.
     * @param nodeId Node id.
     */
    void removeFailedNode(std::uint16_t nodeId) override;

    /**
     * @brief Triggers scan refresh for known nodes.
     */
    void startScan() override;

    /**
     * @brief Requests all config parameters for one node.
     * @param nodeId Node id.
     */
    void requestAllConfigParams(std::uint16_t nodeId) override;

    /**
     * @brief Enables polling for cached values of one node/class pair.
     * @param nodeId Node id.
     * @param classId Command class id.
     */
    void enablePoll(std::uint16_t nodeId, std::uint16_t classId) override;

    /**
     * @brief Requests one immediate state refresh for one node.
     * @param nodeId Node id.
     */
    void requestNodeState(std::uint16_t nodeId) override;

    /**
     * @brief Requests OpenZWave node information interview for one node.
     * @param nodeId Node id.
     */
    void requestNodeInfo(std::uint16_t nodeId) override;

    /**
     * @brief Disconnects driver and releases owned OpenZWave runtime resources.
     * @param devicePath Controller device path used for removal.
     */
    void disconnect(const std::string& devicePath) override;

private:
    static void watcherThunk(OpenZWave::Notification const* notification, void* context);

    [[nodiscard]] bool isNodeReady(std::uint16_t nodeId) const;

    [[nodiscard]] std::uint32_t requireHomeId() const;
    void ensureStarted() const;

    std::string controllerPath_{};
    std::uint8_t logLevel_{2U};
    std::int64_t pollIntervalMs_{kZwaveDefaultPollIntervalMs};

    mutable std::mutex mutex_{};
    ZwaveController* controller_{nullptr};
    OpenZwaveNotificationBridge notificationBridge_{};

    bool started_{false};
    bool watcherInstalled_{false};
    bool driverInstalled_{false};
    bool ownsManager_{false};
    bool ownsOptions_{false};
};

} // namespace yaha
