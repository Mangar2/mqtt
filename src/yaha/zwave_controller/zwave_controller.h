#pragma once

/**
 * @file zwave_controller.h
 * @brief ZWave controller adapter contracts and parity routing behavior.
 */

#include "yaha/message/message.h"
#include "yaha/mqtt_component/mqtt_component.h"
#include "yaha/zwave/zwave_config.h"
#include "yaha/zwave_devices/zwave_devices_mapper.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace yaha {

inline constexpr std::uint16_t kZwaveSwitchBinaryClass = 0x25U;
inline constexpr std::uint16_t kZwaveSwitchMultilevelClass = 0x26U;
inline constexpr std::uint32_t kZwaveUnresponsiveInputTimeoutMs = 180000U;
inline constexpr std::size_t kZwaveUnresponsiveTimeoutErrorThreshold = 100U;

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
  virtual void
  setValue(const ZwaveResolvedId &target,
           const std::variant<bool, double, std::string> &value) = 0;

  /**
   * @brief Writes one configuration parameter.
   * @param nodeId Target node id.
   * @param paramId Config parameter id.
   * @param value Numeric parameter value.
   */
  virtual void setConfigParam(std::uint16_t nodeId, std::uint16_t paramId,
                              double value) = 0;

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
   * @brief Requests one immediate node-state refresh.
   * @param nodeId Node id.
   */
  virtual void requestNodeState(std::uint16_t nodeId) = 0;

  /**
   * @brief Disconnects from configured USB device.
   * @param devicePath USB device path.
   */
  virtual void disconnect(const std::string &devicePath) = 0;
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
  virtual void
  setDeviceConfiguration(const std::vector<ZwaveDeviceConfig> &devices) = 0;

  /**
   * @brief Routes one incoming MQTT set message by topic.
   * @param topic Incoming `/set` topic.
   * @param value Incoming payload.
   * @param reasons Incoming reason chain from the command message.
   */
  virtual void setValue(const std::string &topic, const Value &value,
                        const std::vector<ReasonEntry> &reasons = {}) = 0;

  /**
   * @brief Starts add-node operation.
   */
  virtual void addDevice() = 0;

  /**
   * @brief Starts remove-failed-node operation.
   * @param value Incoming payload containing node id.
   */
  virtual void removeFailedNode(const Value &value) = 0;

  /**
   * @brief Starts controller scan operation.
   */
  virtual void startScan() = 0;

  /**
   * @brief Requests config parameters for all configured nodes.
   */
  virtual void requestConfigParametersForAllNodes() = 0;

  /**
   * @brief Returns node ids currently known by the controller runtime.
   * @return Sorted list of known node ids.
   */
  [[nodiscard]] virtual std::vector<std::uint16_t> knownNodeIds() const = 0;

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
   * @param fullDevicePollIntervalMs Poll interval for full configured-node
   * refresh.
   * @param commandReactionPollIntervalMs Poll interval for tracked command
   * confirmation.
   * @param commandReactionTimeoutMs Timeout for tracked command confirmation.
   */
  ZwaveController(ZwaveUsbConfig usbConfig, IZwaveDriverPort &driverPort,
            std::int64_t fullDevicePollIntervalMs,
            std::int64_t commandReactionPollIntervalMs,
            std::int64_t commandReactionTimeoutMs,
                  std::uint32_t unresponsiveInputTimeoutMs =
                      kZwaveUnresponsiveInputTimeoutMs,
                  std::size_t unresponsiveTimeoutErrorThreshold =
                      kZwaveUnresponsiveTimeoutErrorThreshold);

  /**
   * @brief Destructor.
   */
  ~ZwaveController() override;

  /**
   * @brief Sets publish callback used by controller outputs.
   * @param callback Publish callback.
   */
  void setPublishCallback(PublishCallback callback) override;

  /**
   * @brief Updates mapping configuration used by controller.
   * @param devices Device mapping rows.
   */
  void setDeviceConfiguration(
      const std::vector<ZwaveDeviceConfig> &devices) override;

  /**
   * @brief Routes one incoming MQTT set message by topic.
   * @param topic Incoming `/set` topic.
   * @param value Incoming payload.
   * @param reasons Incoming reason chain from the command message.
   */
  void setValue(const std::string &topic, const Value &value,
                const std::vector<ReasonEntry> &reasons = {}) override;

  /**
   * @brief Starts add-node operation.
   */
  void addDevice() override;

  /**
   * @brief Starts remove-failed-node operation.
   * @param value Incoming payload containing node id.
   */
  void removeFailedNode(const Value &value) override;

  /**
   * @brief Starts controller scan operation.
   */
  void startScan() override;

  /**
   * @brief Requests config parameters for all configured nodes.
   */
  void requestConfigParametersForAllNodes() override;

  /**
   * @brief Returns node ids currently known by the controller runtime.
   * @return Sorted list of known node ids.
   */
  [[nodiscard]] std::vector<std::uint16_t> knownNodeIds() const override;

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
   * @brief Sets callback invoked after driver-failed notification was
   * published.
   * @param callback Callback function.
   */
  void setDriverFailedCallback(std::function<void()> callback);

  /**
   * @brief Sets callback invoked when timeout storm indicates unresponsive
   * ZWave input.
   * @param callback Callback function.
   */
  void setUnresponsiveNetworkCallback(std::function<void()> callback);

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
   * @param nodeId Related node id reported by OpenZWave.
   * @param resultCode Numeric result code.
   * @param statusText Controller status text.
   */
  void onControllerCommand(std::uint16_t nodeId, std::int32_t resultCode,
                           const std::string &statusText);

  /**
   * @brief Handles node-added callback.
   * @param nodeId Node id.
   */
  void onNodeAdded(std::uint16_t nodeId);

  /**
   * @brief Handles node-removed callback and clears runtime state for that
   * node.
   * @param nodeId Node id.
   */
  void onNodeRemoved(std::uint16_t nodeId);

  /**
   * @brief Handles node-ready callback.
   * @param nodeId Node id.
   * @param nodeInfo Node metadata.
   * @param queryStage Query-stage marker from runtime callback.
   */
  void onNodeReady(std::uint16_t nodeId, const ZwaveNodeInfo &nodeInfo,
                   const std::string &queryStage);

  /**
   * @brief Handles value-added callback.
   * @param event Value event payload.
   */
  void onValueAdded(const ZwaveControllerValueEvent &event);

  /**
   * @brief Handles value-removed callback.
   * @param nodeId Node id.
   * @param classId Command class id.
   * @param index Value index.
   */
  void onValueRemoved(std::uint16_t nodeId, std::uint16_t classId,
                      std::uint8_t index);

  /**
   * @brief Handles value-changed callback and publishes mapped message.
   * @param event Value event payload.
   */
  void onValueChanged(const ZwaveControllerValueEvent &event);

  /**
   * @brief Handles value-refreshed callback.
   * @param nodeId Node id.
   * @param classId Command class id.
   * @param event Value event payload.
   */
  void onValueRefreshed(std::uint16_t nodeId, std::uint16_t classId,
                        const ZwaveControllerValueEvent &event);

private:
  struct NodeRuntimeState {
    ZwaveNodeInfo info{};
    bool ready{false};
    bool dead{false};
    std::unordered_map<
        std::uint16_t,
        std::unordered_map<std::uint8_t, ZwaveControllerValueEvent>>
        classes{};
  };

  struct CachedTopicState {
    Value value{std::string{}};
    std::optional<std::uint64_t> valueId{};
  };

  struct PendingCommand {
    std::string replyTopic{};
    ZwaveResolvedId target{};
    Value expectedValue{std::string{}};
    std::vector<ReasonEntry> reasons{};
    std::chrono::steady_clock::time_point sentAt{};
    std::chrono::steady_clock::time_point lastPollAt{};
  };

  struct PendingCommandMatch {
    bool matched{false};
    std::vector<ReasonEntry> reasons{};
  };

  enum class ErrorStateSeverity : std::uint8_t {
    NoError = 0U,
    PublishFailed = 1U,
    DecodeFailed = 2U,
    DriverFailed = 3U
  };

  enum class NodeCommState : std::uint8_t { Ok = 0U, Timeout = 1U };

  enum class NodeHealthState : std::uint8_t {
    Unknown = 0U,
    Alive = 1U,
    Dead = 2U
  };

  [[nodiscard]] static std::optional<std::uint16_t>
  parseNodeIdFromValue(const Value &value);
  [[nodiscard]] static std::optional<std::string>
  parseOptionalLabelFromSetTopic(const std::vector<std::string> &topicParts);
  [[nodiscard]] static std::string
  joinTopicParts(const std::vector<std::string> &parts, std::size_t count);
  [[nodiscard]] static std::vector<std::string>
  splitTopic(const std::string &topic);
  [[nodiscard]] ZwaveNodeMap buildNodeMap() const;
  [[nodiscard]] static ZwaveValueDescriptor
  buildDescriptor(const ZwaveControllerValueEvent &event);
  [[nodiscard]] static std::string
  notificationText(ZwaveNotificationCode notification);
  [[nodiscard]] static bool valuesEquivalent(const Value &leftValue,
                                             const Value &rightValue);
  [[nodiscard]] static Value
  writeValueToExpectedValue(const ZwaveWriteRequest &writeRequest);
  [[nodiscard]] static Value
  toExpectedOutboundValue(const Value &value, const std::string &typeName);
  [[nodiscard]] std::string describeTimeoutSource(std::uint16_t nodeId);
  void
  publishConfigParameterCapabilities(const ZwaveControllerValueEvent &event);

  void rememberPendingCommand(const std::string &replyTopic,
                              const ZwaveWriteRequest &writeRequest,
                              const std::vector<ReasonEntry> &reasons);
  [[nodiscard]] PendingCommandMatch
  takeMatchingPendingReasons(const std::string &replyTopic,
                             const ZwaveControllerValueEvent &event,
                             const Value &outboundValue);
  void cacheLastKnownTopicState(const ZwaveControllerValueEvent &event);
  [[nodiscard]] std::optional<CachedTopicState>
  findCachedTopicState(const std::string &topic) const;
  void publishTimeoutForPendingCommand(const PendingCommand &pendingCommand);
  void pollPendingCommands();
  void pollConfiguredNodes();
  void runPendingCommandPollLoop();
  void markSuccessfulZwaveInput();
  void trackTimeoutDropAndTriggerIfNeeded();

  void publish(const std::string &topic, const Value &value,
               const std::string &reason);
  void publish(const std::string &topic, const Value &value,
               const std::string &reason,
               const std::vector<ReasonEntry> &prependedReasons);
  void publishValue(std::uint16_t nodeId,
                    const ZwaveControllerValueEvent &event,
                    const std::string &reason);
  void storeNodeValue(const ZwaveControllerValueEvent &event);
  void publishNodeState(std::uint16_t nodeId, const std::string &stateName,
                        const std::string &value, const std::string &reason);
  void publishNodeIncludeState(std::uint16_t nodeId, const std::string &value,
                               const std::string &reason,
                               bool forcePublish = false);
  void publishNodeErrorState(std::uint16_t nodeId, const std::string &value,
                             ErrorStateSeverity severity,
                             const std::string &reason);
  void updateNodeHealthState(std::uint16_t nodeId, NodeHealthState targetState,
                             const std::string &reason);
  void updateNodeCommState(std::uint16_t nodeId, NodeCommState targetState,
                           const std::string &reason);
  void clearNodeErrorState(std::uint16_t nodeId, const std::string &reason);
  [[nodiscard]] std::optional<std::string>
  resolveNodeMonitorBaseTopic(std::uint16_t nodeId) const;
  static std::string buildNodeBaseTopic(std::uint16_t nodeId);

  ZwaveUsbConfig usb_{};
  IZwaveDriverPort &driverPort_;

  std::vector<ZwaveDeviceConfig> devices_{};
  mutable std::mutex devicesMutex_{};
  ZwaveDevicesMapper devicesMapper_{std::vector<ZwaveDeviceConfig>{}};
  std::unordered_map<std::uint16_t, NodeRuntimeState> nodes_{};
  std::unordered_map<std::string, CachedTopicState> cachedTopicStates_{};
  mutable std::mutex cachedTopicStatesMutex_{};
  std::unordered_map<std::uint16_t, ErrorStateSeverity> nodeErrorStates_{};
  std::mutex nodeErrorStatesMutex_{};
  std::unordered_map<std::uint16_t, NodeCommState> nodeCommStates_{};
  std::mutex nodeCommStatesMutex_{};
  std::unordered_map<std::uint16_t, NodeHealthState> nodeHealthStates_{};
  std::mutex nodeHealthStatesMutex_{};
  std::unordered_map<std::uint16_t, std::string> nodeIncludeStates_{};
  std::mutex nodeIncludeStatesMutex_{};
  std::unordered_set<std::uint16_t> includeFlowCandidateNodeIds_{};
  std::mutex includeFlowCandidateNodeIdsMutex_{};
  std::unordered_set<std::string> publishedConfigCapabilityKeys_{};
  std::mutex publishedConfigCapabilityKeysMutex_{};
  std::vector<PendingCommand> pendingCommands_{};
  std::mutex pendingCommandsMutex_{};
  std::thread pendingCommandPollThread_{};
  std::atomic_bool pendingCommandPollStop_{false};
  std::chrono::milliseconds fullDevicePollInterval_{
      kZwaveDefaultPollIntervalMs};
  std::chrono::milliseconds commandReactionPollInterval_{
      kZwaveDefaultCommandReactionPollIntervalMs};
  std::chrono::milliseconds commandReactionTimeout_{
      kZwaveDefaultCommandReactionTimeoutMs};
  std::chrono::milliseconds unresponsiveInputTimeout_{std::chrono::minutes{3}};
  std::size_t unresponsiveTimeoutErrorThreshold_{
      kZwaveUnresponsiveTimeoutErrorThreshold};
  std::size_t timeoutErrorsSinceLastSuccess_{0U};
  std::chrono::steady_clock::time_point lastSuccessfulZwaveInputAt_{
      std::chrono::steady_clock::now()};
  bool unresponsiveNetworkCallbackTriggered_{false};
  std::mutex unresponsiveNetworkMutex_{};
  std::chrono::steady_clock::time_point lastFullDevicePollAt_{};
  PublishCallback publishCallback_{};
  std::function<void()> driverFailedCallback_{};
  std::function<void()> unresponsiveNetworkCallback_{};
};

} // namespace yaha
