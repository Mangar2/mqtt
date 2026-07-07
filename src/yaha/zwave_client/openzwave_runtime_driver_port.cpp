#include "yaha/zwave_client/openzwave_runtime_driver_port.h"

#include "yaha/zwave_client/openzwave_write_dispatcher.h"

#include "Driver.h"
#include "Manager.h"
#include "Notification.h"
#include "Options.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <variant>

namespace yaha {
namespace {

constexpr std::uint16_t kRuntimeConfigClass = 0x70U;

constexpr std::uint16_t kPollClassSwitchBinary = 0x25U;
constexpr std::uint16_t kPollClassSwitchMultilevel = 0x26U;
constexpr std::uint16_t kPollClassSensorBinary = 0x30U;
constexpr std::uint16_t kPollClassSensorMultilevel = 0x31U;
constexpr std::uint16_t kPollClassMeter = 0x32U;
constexpr std::uint16_t kPollClassThermostatSetpoint = 0x43U;
constexpr std::uint16_t kPollClassBattery = 0x80U;
constexpr std::uint16_t kPollClassWakeUp = 0x84U;

constexpr std::uint16_t kRuntimePollIntensity = 1U;
constexpr std::uint16_t kNodeIdUpperBound = 255U;

[[nodiscard]] std::string mapLogLevelToOpenZwaveOption(const std::uint8_t logLevel) {
    switch (logLevel) {
    case 0U:
        return "Debug";
    case 1U:
        return "Info";
    case 2U:
        return "Detail";
    case 3U:
        return "Warning";
    case 4U:
        return "Error";
    default:
        return "Info";
    }
}

[[nodiscard]] std::string toOpenZwavePollIntervalOption(const std::int64_t intervalMs) {
    if (intervalMs <= 0) {
        return "5000";
    }
    return std::to_string(intervalMs);
}

[[nodiscard]] std::string defaultConfigDirectory() {
    for (const std::string& candidate : std::array<std::string, 3U>{
             "config",
             "/usr/local/etc/openzwave",
             "/usr/share/openzwave/config"}) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return "config";
}

[[nodiscard]] bool isPollingAllowedClass(const std::uint16_t classId) {
    switch (classId) {
    case kPollClassSwitchBinary:
    case kPollClassSwitchMultilevel:
    case kPollClassSensorBinary:
    case kPollClassSensorMultilevel:
    case kPollClassMeter:
    case kPollClassThermostatSetpoint:
    case kPollClassBattery:
    case kPollClassWakeUp:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] std::uint8_t requireUint8(const std::uint16_t value, const std::string& fieldName) {
    if (value > static_cast<std::uint16_t>(std::numeric_limits<std::uint8_t>::max())) {
        throw std::runtime_error(fieldName + " out of range");
    }
    return static_cast<std::uint8_t>(value);
}

[[nodiscard]] std::string genreNameForLog(const std::uint8_t cachedGenreCode) {
    switch (cachedGenreCode) {
    case 0U:
        return "basic";
    case 1U:
        return "user";
    case 2U:
        return "config";
    case 3U:
        return "system";
    default:
        return "unknown";
    }
}

} // namespace

OpenZwaveRuntimeDriverPort::OpenZwaveRuntimeDriverPort(
    std::string controllerPath,
    const std::uint8_t logLevel,
    const std::int64_t pollIntervalMs)
    : controllerPath_(std::move(controllerPath)),
      logLevel_(logLevel),
      pollIntervalMs_(pollIntervalMs) {
}

OpenZwaveRuntimeDriverPort::~OpenZwaveRuntimeDriverPort() {
    try {
        disconnect(controllerPath_);
    } catch (...) {
    }
}

void OpenZwaveRuntimeDriverPort::bindController(ZwaveController& controller) {
    std::scoped_lock lock{mutex_};
    controller_ = &controller;
    notificationBridge_.bindController(controller_);
}

void OpenZwaveRuntimeDriverPort::start() {
    std::scoped_lock lock{mutex_};
    if (started_) {
        return;
    }

    OpenZWave::Options* options = OpenZWave::Options::Get();
    if (options == nullptr) {
        OpenZWave::Options::Create(defaultConfigDirectory(), ".", "");
        options = OpenZWave::Options::Get();
        if (options == nullptr) {
            throw std::runtime_error("OpenZWave options create failed");
        }

        options->AddOptionBool("ConsoleOutput", false);
        options->AddOptionBool("Logging", true);
        options->AddOptionBool("SaveLogLevel", false);
        options->AddOptionBool("QueueLogLevel", false);
        options->AddOptionBool("AppendLogFile", false);
        options->AddOptionBool("NotifyTransactions", true);
        options->AddOptionInt("PollInterval", std::stoi(toOpenZwavePollIntervalOption(pollIntervalMs_)));
        options->AddOptionString("SaveConfiguration", "true", false);
        options->AddOptionString("SaveConfigurationTime", "5", false);
        options->AddOptionString("LogLevel", mapLogLevelToOpenZwaveOption(logLevel_), false);
        options->Lock();
        ownsOptions_ = true;
    }

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        OpenZWave::Manager::Create();
        manager = OpenZWave::Manager::Get();
        if (manager == nullptr) {
            throw std::runtime_error("OpenZWave manager create failed");
        }
        ownsManager_ = true;
    }

    manager->AddWatcher(&OpenZwaveRuntimeDriverPort::watcherThunk, this);
    watcherInstalled_ = true;

    if (!manager->AddDriver(controllerPath_)) {
        manager->RemoveWatcher(&OpenZwaveRuntimeDriverPort::watcherThunk, this);
        watcherInstalled_ = false;
        throw std::runtime_error("OpenZWave AddDriver failed for path: " + controllerPath_);
    }

    driverInstalled_ = true;
    started_ = true;
}

void OpenZwaveRuntimeDriverPort::ensureStarted() const {
    if (!started_) {
        throw std::runtime_error("OpenZWave runtime driver is not started");
    }
}

void OpenZwaveRuntimeDriverPort::addNode() {
    ensureStarted();

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    if (!manager->AddNode(requireHomeId(), false)) {
        throw std::runtime_error("AddNode failed");
    }
}

void OpenZwaveRuntimeDriverPort::removeFailedNode(const std::uint16_t nodeId) {
    ensureStarted();

    if (!isNodeReady(nodeId)) {
        throw std::runtime_error("Node not ready yet");
    }

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    if (!manager->RemoveFailedNode(requireHomeId(), requireUint8(nodeId, "node id"))) {
        throw std::runtime_error("RemoveFailedNode failed");
    }
}

void OpenZwaveRuntimeDriverPort::startScan() {
    ensureStarted();

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    const std::uint32_t homeId = requireHomeId();
    std::unordered_set<std::uint16_t> targets{};

    for (const std::uint16_t nodeId : notificationBridge_.collectKnownNodes()) {
        if (nodeId == 0U || nodeId > kNodeIdUpperBound || !isNodeReady(nodeId)) {
            continue;
        }
        targets.insert(nodeId);
    }

    if (targets.empty()) {
        for (std::uint16_t nodeId = 1U; nodeId <= kNodeIdUpperBound; ++nodeId) {
            if (isNodeReady(nodeId)) {
                targets.insert(nodeId);
            }
        }
    }

    for (const std::uint16_t nodeId : targets) {
        manager->RequestNodeState(homeId, requireUint8(nodeId, "node id"));
    }
}

void OpenZwaveRuntimeDriverPort::setValue(
    const ZwaveResolvedId& target,
    const std::variant<bool, double, std::string>& value) {
    ensureStarted();
    if (!isNodeReady(target.nodeId)) {
        throw std::runtime_error("Node not ready yet");
    }

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    const std::uint32_t homeId = requireHomeId();
    const auto discovery = notificationBridge_.snapshotDiscoveryState(target);
    const auto metadata = OpenZwaveWriteDispatcher::buildWriteMetadata(target, discovery.genreCode, value);

    const bool writeAccepted = OpenZwaveWriteDispatcher::write(*manager, homeId, target, metadata, value);
    if (!writeAccepted) {
        if (controller_ != nullptr) {
            controller_->onNotification(target.nodeId, ZwaveNotificationCode::NodeDead);
        }

        throw std::runtime_error(
            "SetValue failed for node=" + std::to_string(target.nodeId)
            + " class=0x" + std::to_string(target.classId)
            + " instance=" + std::to_string(target.instance)
            + " index=" + std::to_string(target.index)
            + " genre=" + genreNameForLog(discovery.genreCode)
            + " valueType=" + metadata.valueTypeName
            + " payload=" + metadata.payloadText);
    }
}

void OpenZwaveRuntimeDriverPort::setConfigParam(
    const std::uint16_t nodeId,
    const std::uint16_t paramId,
    const double value) {
    ensureStarted();
    if (!isNodeReady(nodeId)) {
        throw std::runtime_error("Node not ready yet");
    }

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    (void)kRuntimeConfigClass;
    OpenZwaveWriteDispatcher::setConfigParam(*manager, requireHomeId(), nodeId, paramId, value);
}

void OpenZwaveRuntimeDriverPort::requestAllConfigParams(const std::uint16_t nodeId) {
    ensureStarted();
    if (!isNodeReady(nodeId)) {
        throw std::runtime_error("Node not ready yet");
    }

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    constexpr std::uint16_t maxParamId = 127U;
    const std::uint32_t homeId = requireHomeId();
    for (std::uint16_t paramId = 1U; paramId <= maxParamId; ++paramId) {
        manager->RequestConfigParam(homeId, requireUint8(nodeId, "node id"), requireUint8(paramId, "param id"));
    }
}

void OpenZwaveRuntimeDriverPort::enablePoll(const std::uint16_t nodeId, const std::uint16_t classId) {
    ensureStarted();

    if (!isNodeReady(nodeId) || !isPollingAllowedClass(classId)) {
        return;
    }

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        return;
    }

    const std::uint32_t homeId = requireHomeId();
    for (const auto& entry : notificationBridge_.collectPollValueEntries(nodeId, classId)) {
        if (!notificationBridge_.markPollEnabled(entry.rawValueId)) {
            continue;
        }

        const OpenZWave::ValueID valueId{homeId, entry.rawValueId};
        (void)manager->EnablePoll(valueId, kRuntimePollIntensity);
    }
}

void OpenZwaveRuntimeDriverPort::requestNodeState(const std::uint16_t nodeId) {
    ensureStarted();
    if (!isNodeReady(nodeId)) {
        throw std::runtime_error("Node not ready yet");
    }

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    manager->RequestNodeState(requireHomeId(), requireUint8(nodeId, "node id"));
}

void OpenZwaveRuntimeDriverPort::requestNodeInfo(const std::uint16_t nodeId) {
    ensureStarted();
    if (!isNodeReady(nodeId)) {
        throw std::runtime_error("Node not ready yet");
    }

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    manager->RequestNodeDynamic(requireHomeId(), requireUint8(nodeId, "node id"));
}

void OpenZwaveRuntimeDriverPort::disconnect(const std::string& devicePath) {
    std::scoped_lock lock{mutex_};

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager != nullptr) {
        if (watcherInstalled_) {
            manager->RemoveWatcher(&OpenZwaveRuntimeDriverPort::watcherThunk, this);
            watcherInstalled_ = false;
        }

        if (driverInstalled_) {
            manager->RemoveDriver(devicePath.empty() ? controllerPath_ : devicePath);
            driverInstalled_ = false;
        }

        if (ownsManager_) {
            OpenZWave::Manager::Destroy();
            ownsManager_ = false;
        }
    }

    OpenZWave::Options* options = OpenZWave::Options::Get();
    if (options != nullptr && ownsOptions_) {
        OpenZWave::Options::Destroy();
        ownsOptions_ = false;
    }

    started_ = false;
    notificationBridge_.reset();
}

void OpenZwaveRuntimeDriverPort::watcherThunk(OpenZWave::Notification const* notification, void* context) {
    if (notification == nullptr || context == nullptr) {
        return;
    }

    auto* port = static_cast<OpenZwaveRuntimeDriverPort*>(context);
    port->notificationBridge_.handleNotification(*notification);
}

bool OpenZwaveRuntimeDriverPort::isNodeReady(const std::uint16_t nodeId) const {
    return notificationBridge_.isNodeReady(nodeId);
}

std::uint32_t OpenZwaveRuntimeDriverPort::requireHomeId() const {
    return notificationBridge_.requireHomeId();
}

} // namespace yaha
