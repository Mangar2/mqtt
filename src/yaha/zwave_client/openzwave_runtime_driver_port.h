#pragma once

/**
 * @file openzwave_runtime_driver_port.h
 * @brief OpenZWave runtime-backed implementation of the ZWave driver port.
 */

#include "yaha/zwave_controller/zwave_controller.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace OpenZWave {
class Notification;
class ValueID;
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
     */
    explicit OpenZwaveRuntimeDriverPort(std::string controllerPath);

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
     * @brief Disconnects driver and releases owned OpenZWave runtime resources.
     * @param devicePath Controller device path used for removal.
     */
    void disconnect(const std::string& devicePath) override;

private:
    using ValueIndexMap = std::unordered_map<std::uint16_t, std::uint64_t>;
    using ValueClassMap = std::unordered_map<std::uint16_t, ValueIndexMap>;

    static void watcherThunk(OpenZWave::Notification const* notification, void* context);

    void handleNotification(OpenZWave::Notification const& notification);
    void handleValueAddedOrChanged(OpenZWave::Notification const& notification, bool changed);
    void handleValueRemoved(OpenZWave::Notification const& notification);

    [[nodiscard]] static ZwaveNodeInfo buildNodeInfo(std::uint32_t homeId, std::uint16_t nodeId);
    [[nodiscard]] static ZwaveControllerValueEvent buildValueEvent(OpenZWave::ValueID const& valueId);

    [[nodiscard]] static std::string toLower(std::string value);
    [[nodiscard]] static std::string valueTypeName(OpenZWave::ValueID const& valueId);
    [[nodiscard]] static std::string controllerStateText(std::uint8_t stateCode);

    [[nodiscard]] std::uint32_t requireHomeId() const;
    void ensureStarted();

    std::string controllerPath_{};

    mutable std::mutex mutex_{};
    ZwaveController* controller_{nullptr};

    bool started_{false};
    bool watcherInstalled_{false};
    bool driverInstalled_{false};
    bool ownsManager_{false};
    bool ownsOptions_{false};

    std::uint32_t homeId_{0U};
    std::unordered_set<std::uint16_t> knownNodes_{};
    std::unordered_map<std::uint16_t, ValueClassMap> valueIdCache_{};
};

} // namespace yaha
