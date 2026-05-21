#include "yaha/zwave_client/openzwave_runtime_driver_port.h"

#include "Driver.h"
#include "Manager.h"
#include "Notification.h"
#include "Options.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>

namespace yaha {

namespace {

using RuntimeValueIndexMap = std::unordered_map<std::uint16_t, std::uint64_t>;
using RuntimeValueInstanceMap = std::unordered_map<std::uint8_t, RuntimeValueIndexMap>;
using RuntimeValueClassMap = std::unordered_map<std::uint16_t, RuntimeValueInstanceMap>;

constexpr std::uint16_t kMaxNodeId = 255U;
constexpr std::uint16_t kRuntimeSwitchMultilevelClass = 0x26U;
constexpr std::uint16_t kConfigParamValueSize = 2U;
constexpr float kNumericTolerance = 1e-6F;
constexpr std::uint8_t kPollIntensity = 1U;
constexpr int kOzwLogLevelNone = 1;
constexpr int kOzwLogLevelError = 4;
constexpr int kOzwLogLevelInfo = 7;
constexpr int kOzwLogLevelDetail = 8;
constexpr int kOzwLogLevelDebug = 9;
constexpr int kOzwLogLevelStreamDetail = 10;

[[nodiscard]] int mapSaveLogLevel(const std::uint8_t logLevel) {
    switch (logLevel) {
    case 0U:
        return kOzwLogLevelNone;
    case 1U:
        return kOzwLogLevelError;
    case 2U:
        return kOzwLogLevelInfo;
    case 3U:
        return kOzwLogLevelDetail;
    case 4U:
    default:
        return kOzwLogLevelDebug;
    }
}

[[nodiscard]] int mapQueueLogLevel(const std::uint8_t logLevel) {
    switch (logLevel) {
    case 0U:
        return kOzwLogLevelNone;
    case 1U:
        return kOzwLogLevelError;
    case 2U:
        return kOzwLogLevelDetail;
    case 3U:
        return kOzwLogLevelDebug;
    case 4U:
    default:
        return kOzwLogLevelStreamDetail;
    }
}

[[nodiscard]] std::filesystem::path repositoryRoot() {
    return std::filesystem::current_path();
}

[[nodiscard]] std::filesystem::path deploymentRootForPathLookup() {
    std::filesystem::path currentPath = repositoryRoot();
    if (currentPath.filename() == "zwave") {
        return currentPath.parent_path();
    }
    return currentPath;
}

[[nodiscard]] std::filesystem::path openzwaveConfigPath() {
    if (const char* configuredPath = std::getenv("YAHA_OPENZWAVE_CONFIG_PATH");
        configuredPath != nullptr && configuredPath[0] != '\0') {
        const std::filesystem::path environmentPath{configuredPath};
        if (!std::filesystem::exists(environmentPath)) {
            throw std::runtime_error("OpenZWave config path not found: " + environmentPath.string());
        }
        return environmentPath;
    }

    const std::filesystem::path lookupRoot = deploymentRootForPathLookup();
    const std::array<std::filesystem::path, 4> candidates{
        lookupRoot / "third_party/openzwave/config",
        repositoryRoot() / "third_party/openzwave/config",
        repositoryRoot().parent_path() / "third_party/openzwave/config",
        std::filesystem::path{"/usr/share/openzwave/config"},
    };

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    throw std::runtime_error("OpenZWave config path not found: " + candidates.front().string());
}

[[nodiscard]] std::filesystem::path openzwaveUserPath() {
    if (const char* configuredPath = std::getenv("YAHA_OPENZWAVE_USER_PATH");
        configuredPath != nullptr && configuredPath[0] != '\0') {
        return std::filesystem::path{configuredPath};
    }

    return deploymentRootForPathLookup() / "tmp/openzwave";
}

[[nodiscard]] std::uint8_t requireUint8(const std::uint16_t value, const std::string& fieldName) {
    if (value > static_cast<std::uint16_t>(std::numeric_limits<std::uint8_t>::max())) {
        throw std::runtime_error(fieldName + " out of range");
    }
    return static_cast<std::uint8_t>(value);
}

[[nodiscard]] std::uint8_t clampIndexToUint8(const std::uint16_t value) {
    return value > static_cast<std::uint16_t>(std::numeric_limits<std::uint8_t>::max())
        ? std::numeric_limits<std::uint8_t>::max()
        : static_cast<std::uint8_t>(value);
}

[[nodiscard]] int32_t roundToInt32(const double value, const std::string& fieldName) {
    const double rounded = std::round(value);
    if (std::fabs(rounded - value) > kNumericTolerance) {
        throw std::runtime_error(fieldName + " requires integer value");
    }

    if (rounded < static_cast<double>(std::numeric_limits<int32_t>::min())
        || rounded > static_cast<double>(std::numeric_limits<int32_t>::max())) {
        throw std::runtime_error(fieldName + " out of range");
    }

    return static_cast<int32_t>(rounded);
}

[[nodiscard]] OpenZWave::ValueID::ValueType toValueType(const std::string& typeName) {
    if (typeName == "bool" || typeName == "switch") {
        return OpenZWave::ValueID::ValueType_Bool;
    }

    if (typeName == "byte") {
        return OpenZWave::ValueID::ValueType_Byte;
    }

    if (typeName == "short") {
        return OpenZWave::ValueID::ValueType_Short;
    }

    if (typeName == "int" || typeName == "integer") {
        return OpenZWave::ValueID::ValueType_Int;
    }

    if (typeName == "decimal" || typeName == "float" || typeName == "number") {
        return OpenZWave::ValueID::ValueType_Decimal;
    }

    if (typeName == "list") {
        return OpenZWave::ValueID::ValueType_List;
    }

    if (typeName == "button") {
        return OpenZWave::ValueID::ValueType_Button;
    }

    return OpenZWave::ValueID::ValueType_String;
}

[[nodiscard]] OpenZWave::ValueID resolveCachedValueId(
    const std::uint32_t homeId,
    const ZwaveResolvedId& target,
    const RuntimeValueClassMap& classMap) {
    const auto classIterator = classMap.find(target.classId);
    if (classIterator == classMap.end()) {
        return OpenZWave::ValueID{};
    }

    const auto instanceIterator = classIterator->second.find(target.instance);
    if (instanceIterator == classIterator->second.end()) {
        return OpenZWave::ValueID{};
    }

    const auto indexIterator = instanceIterator->second.find(target.index);
    if (indexIterator == instanceIterator->second.end()) {
        return OpenZWave::ValueID{};
    }

    return {homeId, indexIterator->second};
}

[[nodiscard]] OpenZWave::ValueID buildFallbackValueId(
    const std::uint32_t homeId,
    const ZwaveResolvedId& target,
    const OpenZWave::ValueID::ValueType valueType) {
    return OpenZWave::ValueID{
        homeId,
        requireUint8(target.nodeId, "node id"),
        OpenZWave::ValueID::ValueGenre_User,
        requireUint8(target.classId, "class id"),
        requireUint8(target.instance, "instance"),
        target.index,
        valueType};
}

bool writeBooleanValue(
    OpenZWave::Manager& manager,
    const OpenZWave::ValueID& valueId,
    const OpenZWave::ValueID::ValueType valueType,
    const bool booleanValue) {
    if (valueType == OpenZWave::ValueID::ValueType_Bool) {
        return manager.SetValue(valueId, booleanValue);
    }

    if (valueType == OpenZWave::ValueID::ValueType_Byte) {
        return manager.SetValue(valueId, static_cast<std::uint8_t>(booleanValue ? 1U : 0U));
    }

    if (valueType == OpenZWave::ValueID::ValueType_Short) {
        return manager.SetValue(valueId, static_cast<int16_t>(booleanValue ? 1 : 0));
    }

    if (valueType == OpenZWave::ValueID::ValueType_Int) {
        return manager.SetValue(valueId, static_cast<int32_t>(booleanValue ? 1 : 0));
    }

    if (valueType == OpenZWave::ValueID::ValueType_Decimal) {
        return manager.SetValue(valueId, booleanValue ? 1.0F : 0.0F);
    }

    if (valueType == OpenZWave::ValueID::ValueType_List) {
        return manager.SetValueListSelection(valueId, booleanValue ? "on" : "off");
    }

    return manager.SetValue(valueId, std::string{booleanValue ? "on" : "off"});
}

bool writeNumericValue(
    OpenZWave::Manager& manager,
    const OpenZWave::ValueID& valueId,
    const OpenZWave::ValueID::ValueType valueType,
    const double numericValue) {
    if (valueType == OpenZWave::ValueID::ValueType_Bool) {
        return manager.SetValue(valueId, std::fabs(numericValue - 0.0) > kNumericTolerance);
    }

    if (valueType == OpenZWave::ValueID::ValueType_Byte) {
        const int32_t rounded = roundToInt32(numericValue, "byte value");
        if (rounded < 0 || rounded > static_cast<int32_t>(std::numeric_limits<std::uint8_t>::max())) {
            throw std::runtime_error("byte value out of range");
        }
        return manager.SetValue(valueId, static_cast<std::uint8_t>(rounded));
    }

    if (valueType == OpenZWave::ValueID::ValueType_Short) {
        const int32_t rounded = roundToInt32(numericValue, "short value");
        if (rounded < static_cast<int32_t>(std::numeric_limits<int16_t>::min())
            || rounded > static_cast<int32_t>(std::numeric_limits<int16_t>::max())) {
            throw std::runtime_error("short value out of range");
        }
        return manager.SetValue(valueId, static_cast<int16_t>(rounded));
    }

    if (valueType == OpenZWave::ValueID::ValueType_Int) {
        return manager.SetValue(valueId, roundToInt32(numericValue, "int value"));
    }

    if (valueType == OpenZWave::ValueID::ValueType_Decimal) {
        return manager.SetValue(valueId, static_cast<float>(numericValue));
    }

    return manager.SetValue(valueId, std::to_string(numericValue));
}

bool writeTextValue(
    OpenZWave::Manager& manager,
    const OpenZWave::ValueID& valueId,
    const OpenZWave::ValueID::ValueType valueType,
    const std::string& textValue) {
    if (valueType == OpenZWave::ValueID::ValueType_List) {
        return manager.SetValueListSelection(valueId, textValue);
    }

    return manager.SetValue(valueId, textValue);
}

bool applyTypedValueWrite(
    OpenZWave::Manager& manager,
    const OpenZWave::ValueID& valueId,
    const OpenZWave::ValueID::ValueType valueType,
    const std::variant<bool, double, std::string>& value) {
    if (const auto* booleanValue = std::get_if<bool>(&value); booleanValue != nullptr) {
        return writeBooleanValue(manager, valueId, valueType, *booleanValue);
    }

    if (const auto* numericValue = std::get_if<double>(&value); numericValue != nullptr) {
        return writeNumericValue(manager, valueId, valueType, *numericValue);
    }

    return writeTextValue(manager, valueId, valueType, std::get<std::string>(value));
}

} // namespace

OpenZwaveRuntimeDriverPort::OpenZwaveRuntimeDriverPort(
    std::string controllerPath,
    const std::uint8_t logLevel,
    const std::uint32_t pollIntervalMs)
    : controllerPath_(std::move(controllerPath))
    , logLevel_(logLevel) {
    pollIntervalMs_ = pollIntervalMs;
}

OpenZwaveRuntimeDriverPort::~OpenZwaveRuntimeDriverPort() {
    try {
        disconnect(controllerPath_);
    } catch (...) {
    }
}

void OpenZwaveRuntimeDriverPort::start() {
    ensureStarted();
}

void OpenZwaveRuntimeDriverPort::bindController(ZwaveController& controller) {
    std::scoped_lock lock{mutex_};
    controller_ = &controller;
}

void OpenZwaveRuntimeDriverPort::setValue(
    const ZwaveResolvedId& target,
    const std::variant<bool, double, std::string>& value) {
    ensureStarted();

    const std::uint32_t homeId = requireHomeId();
    const std::string typeName = toLower(target.type);
    const auto valueType = toValueType(typeName);
    auto fallbackValueType = valueType;
    if (valueType == OpenZWave::ValueID::ValueType_Bool && target.classId == kRuntimeSwitchMultilevelClass) {
        // Switch Multilevel writes use numeric levels in OpenZWave (e.g. 0/99), not bool.
        fallbackValueType = OpenZWave::ValueID::ValueType_Byte;
    }

    OpenZWave::ValueID valueId{};
    {
        std::scoped_lock lock{mutex_};
        const auto nodeIterator = valueIdCache_.find(target.nodeId);
        if (nodeIterator != valueIdCache_.end()) {
            valueId = resolveCachedValueId(homeId, target, nodeIterator->second);
        }
    }

    if (valueId.GetId() == 0U) {
        valueId = buildFallbackValueId(homeId, target, fallbackValueType);
    }

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    const auto resolvedValueType = valueId.GetType();
    const bool writeAccepted = applyTypedValueWrite(*manager, valueId, resolvedValueType, value);
    if (!writeAccepted) {
        ZwaveController* controller = nullptr;
        {
            std::scoped_lock lock{mutex_};
            controller = controller_;
        }
        if (controller != nullptr) {
            controller->onNotification(target.nodeId, ZwaveNotificationCode::NodeDead);
        }
        throw std::runtime_error(
            "OpenZWave rejected setValue for node " + std::to_string(target.nodeId) + " (presumed dead)");
    }
}

void OpenZwaveRuntimeDriverPort::setConfigParam(
    const std::uint16_t nodeId,
    const std::uint16_t paramId,
    const double value) {
    ensureStarted();

    const std::uint32_t homeId = requireHomeId();
    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    (void)manager->SetConfigParam(
        homeId,
        requireUint8(nodeId, "node id"),
        requireUint8(paramId, "param id"),
        roundToInt32(value, "config param value"),
        static_cast<std::uint8_t>(kConfigParamValueSize));
}

void OpenZwaveRuntimeDriverPort::addNode() {
    ensureStarted();

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    (void)manager->AddNode(requireHomeId(), true);
}

void OpenZwaveRuntimeDriverPort::removeFailedNode(const std::uint16_t nodeId) {
    ensureStarted();

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    (void)manager->RemoveFailedNode(requireHomeId(), requireUint8(nodeId, "node id"));
}

void OpenZwaveRuntimeDriverPort::startScan() {
    ensureStarted();

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    const std::uint32_t homeId = requireHomeId();

    std::scoped_lock lock{mutex_};
    for (const std::uint16_t nodeId : knownNodes_) {
        if (nodeId == 0U || nodeId > kMaxNodeId) {
            continue;
        }
        (void)manager->RequestNodeState(homeId, static_cast<std::uint8_t>(nodeId));
    }
}

void OpenZwaveRuntimeDriverPort::requestAllConfigParams(const std::uint16_t nodeId) {
    ensureStarted();

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    manager->RequestAllConfigParams(requireHomeId(), requireUint8(nodeId, "node id"));
}

void OpenZwaveRuntimeDriverPort::enablePoll(const std::uint16_t nodeId, const std::uint16_t classId) {
    ensureStarted();

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    const std::uint32_t homeId = requireHomeId();

    std::scoped_lock lock{mutex_};

    const auto nodeIterator = valueIdCache_.find(nodeId);
    if (nodeIterator == valueIdCache_.end()) {
        return;
    }

    const auto classIterator = nodeIterator->second.find(classId);
    if (classIterator == nodeIterator->second.end()) {
        return;
    }

    for (const auto& [instance, valuesByIndex] : classIterator->second) {
        (void)instance;
        for (const auto& [index, valueId] : valuesByIndex) {
            (void)index;
            (void)manager->EnablePoll(OpenZWave::ValueID(homeId, valueId), kPollIntensity);
        }
    }
}

void OpenZwaveRuntimeDriverPort::requestNodeState(const std::uint16_t nodeId) {
    ensureStarted();

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    (void)manager->RequestNodeState(requireHomeId(), requireUint8(nodeId, "node id"));
}

void OpenZwaveRuntimeDriverPort::disconnect(const std::string& devicePath) {
    std::scoped_lock lock{mutex_};

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager != nullptr) {
        if (driverInstalled_) {
            const std::string removePath = devicePath.empty() ? controllerPath_ : devicePath;
            if (!removePath.empty()) {
                (void)manager->RemoveDriver(removePath);
            }
            driverInstalled_ = false;
        }

        if (watcherInstalled_) {
            (void)manager->RemoveWatcher(&OpenZwaveRuntimeDriverPort::watcherThunk, this);
            watcherInstalled_ = false;
        }
    }

    if (ownsManager_) {
        OpenZWave::Manager::Destroy();
        ownsManager_ = false;
    }

    if (ownsOptions_) {
        (void)OpenZWave::Options::Destroy();
        ownsOptions_ = false;
    }

    started_ = false;
    homeId_ = 0U;
    knownNodes_.clear();
    valueIdCache_.clear();
}

void OpenZwaveRuntimeDriverPort::watcherThunk(OpenZWave::Notification const* notification, void* context) {
    if (notification == nullptr || context == nullptr) {
        return;
    }

    auto* self = static_cast<OpenZwaveRuntimeDriverPort*>(context);
    try {
        self->handleNotification(*notification);
    } catch (const std::exception& exceptionValue) {
        std::cerr << "zwave_client[error] op=watcher_notification"
                  << " type=" << static_cast<int>(notification->GetType())
                  << " node=" << notification->GetNodeId()
                  << " detail=\"" << exceptionValue.what() << "\""
                  << '\n' << std::flush;
    } catch (...) {
        std::cerr << "zwave_client[error] op=watcher_notification"
                  << " type=" << static_cast<int>(notification->GetType())
                  << " node=" << notification->GetNodeId()
                  << " detail=\"unknown\""
                  << '\n' << std::flush;
    }
}

void OpenZwaveRuntimeDriverPort::handleNotification(OpenZWave::Notification const& notification) {
    ZwaveController* controller = nullptr;
    {
        std::scoped_lock lock{mutex_};
        controller = controller_;
    }
    if (controller == nullptr) {
        return;
    }

    const auto type = notification.GetType();
    const std::uint16_t nodeId = notification.GetNodeId();

    switch (type) {
    case OpenZWave::Notification::Type_DriverReady:
        {
            std::scoped_lock lock{mutex_};
            homeId_ = notification.GetHomeId();
            knownNodes_.insert(nodeId);
        }
        controller->onDriverReady(notification.GetHomeId());
        return;
    case OpenZWave::Notification::Type_DriverFailed:
        controller->onDriverFailed();
        return;
    case OpenZWave::Notification::Type_NodeAdded:
    case OpenZWave::Notification::Type_NodeNew:
        {
            std::scoped_lock lock{mutex_};
            knownNodes_.insert(nodeId);
        }
        controller->onNodeAdded(nodeId);
        return;
    case OpenZWave::Notification::Type_NodeQueriesComplete:
    case OpenZWave::Notification::Type_EssentialNodeQueriesComplete:
        controller->onNodeReady(nodeId, buildNodeInfo(notification.GetHomeId(), nodeId));
        return;
    case OpenZWave::Notification::Type_ValueAdded:
        handleValueAddedOrChanged(notification, false);
        return;
    case OpenZWave::Notification::Type_ValueChanged:
        handleValueAddedOrChanged(notification, true);
        return;
    case OpenZWave::Notification::Type_ValueRefreshed:
        {
            const ZwaveControllerValueEvent event = buildValueEvent(notification.GetValueID());
            controller->onValueRefreshed(nodeId, event.classId, event);
        }
        return;
    case OpenZWave::Notification::Type_ValueRemoved:
        handleValueRemoved(notification);
        return;
    case OpenZWave::Notification::Type_Notification:
        {
            const std::uint8_t code = notification.GetNotification();
            if (code <= static_cast<std::uint8_t>(ZwaveNotificationCode::NodeAlive)) {
                controller->onNotification(nodeId, static_cast<ZwaveNotificationCode>(code));
            }
        }
        return;
    case OpenZWave::Notification::Type_ControllerCommand:
        {
            const auto stateCode = notification.GetNotification();
            controller->onControllerCommand(
                static_cast<std::int32_t>(stateCode),
                controllerStateText(stateCode));
            if (stateCode == OpenZWave::Driver::ControllerState_NodeFailed) {
                controller->onNotification(nodeId, ZwaveNotificationCode::NodeDead);
            } else if (stateCode == OpenZWave::Driver::ControllerState_NodeOK) {
                controller->onNotification(nodeId, ZwaveNotificationCode::NodeAlive);
            }
        }
        return;
    case OpenZWave::Notification::Type_AllNodesQueried:
    case OpenZWave::Notification::Type_AllNodesQueriedSomeDead:
        controller->onScanComplete();
        return;
    default:
        return;
    }
}

void OpenZwaveRuntimeDriverPort::handleValueAddedOrChanged(
    OpenZWave::Notification const& notification,
    const bool changed) {
    const OpenZWave::ValueID valueId = notification.GetValueID();

    {
        std::scoped_lock lock{mutex_};
        knownNodes_.insert(valueId.GetNodeId());
        valueIdCache_[valueId.GetNodeId()][valueId.GetCommandClassId()][valueId.GetInstance()][valueId.GetIndex()] =
            valueId.GetId();
    }

    ZwaveController* controller = nullptr;
    {
        std::scoped_lock lock{mutex_};
        controller = controller_;
    }
    if (controller == nullptr) {
        return;
    }

    const ZwaveControllerValueEvent event = buildValueEvent(valueId);
    if (changed) {
        controller->onValueChanged(event);
    } else {
        controller->onValueAdded(event);
    }
}

void OpenZwaveRuntimeDriverPort::handleValueRemoved(OpenZWave::Notification const& notification) {
    const OpenZWave::ValueID valueId = notification.GetValueID();

    {
        std::scoped_lock lock{mutex_};
        auto nodeIterator = valueIdCache_.find(valueId.GetNodeId());
        if (nodeIterator != valueIdCache_.end()) {
            auto classIterator = nodeIterator->second.find(valueId.GetCommandClassId());
            if (classIterator != nodeIterator->second.end()) {
                auto instanceIterator = classIterator->second.find(valueId.GetInstance());
                if (instanceIterator != classIterator->second.end()) {
                    instanceIterator->second.erase(valueId.GetIndex());
                }
            }
        }
    }

    ZwaveController* controller = nullptr;
    {
        std::scoped_lock lock{mutex_};
        controller = controller_;
    }
    if (controller == nullptr) {
        return;
    }

    controller->onValueRemoved(
        valueId.GetNodeId(),
        valueId.GetCommandClassId(),
        clampIndexToUint8(valueId.GetIndex()));
}

ZwaveNodeInfo OpenZwaveRuntimeDriverPort::buildNodeInfo(const std::uint32_t homeId, const std::uint16_t nodeId) {
    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    const auto nodeIdentifier = requireUint8(nodeId, "node id");
    return ZwaveNodeInfo{
        .manufacturer = manager->GetNodeManufacturerName(homeId, nodeIdentifier),
        .manufacturerId = manager->GetNodeManufacturerId(homeId, nodeIdentifier),
        .product = manager->GetNodeProductName(homeId, nodeIdentifier),
        .productType = manager->GetNodeProductType(homeId, nodeIdentifier),
        .productId = manager->GetNodeProductId(homeId, nodeIdentifier),
        .type = manager->GetNodeType(homeId, nodeIdentifier),
        .name = manager->GetNodeName(homeId, nodeIdentifier),
        .location = manager->GetNodeLocation(homeId, nodeIdentifier)};
}

ZwaveControllerValueEvent OpenZwaveRuntimeDriverPort::buildValueEvent(OpenZWave::ValueID const& valueId) {
    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        throw std::runtime_error("OpenZWave manager unavailable");
    }

    ZwaveControllerValueEvent event{};
    event.nodeId = valueId.GetNodeId();
    event.classId = valueId.GetCommandClassId();
    event.instance = valueId.GetInstance();
    event.index = clampIndexToUint8(valueId.GetIndex());
    event.label = manager->GetValueLabel(valueId);
    event.valueId = valueId.GetId();
    event.type = valueTypeName(valueId);
    event.readOnly = manager->IsValueReadOnly(valueId);

    switch (valueId.GetType()) {
    case OpenZWave::ValueID::ValueType_Bool:
        {
            bool currentValue = false;
            if (manager->GetValueAsBool(valueId, &currentValue)) {
                event.value = currentValue ? 1.0 : 0.0;
            } else {
                event.value = std::string{"0"};
            }
        }
        break;
    case OpenZWave::ValueID::ValueType_Byte:
        {
            std::uint8_t currentValue = 0U;
            if (manager->GetValueAsByte(valueId, &currentValue)) {
                event.value = static_cast<double>(currentValue);
            } else {
                event.value = std::string{"0"};
            }
        }
        break;
    case OpenZWave::ValueID::ValueType_Short:
        {
            int16_t currentValue = 0;
            if (manager->GetValueAsShort(valueId, &currentValue)) {
                event.value = static_cast<double>(currentValue);
            } else {
                event.value = std::string{"0"};
            }
        }
        break;
    case OpenZWave::ValueID::ValueType_Int:
        {
            int32_t currentValue = 0;
            if (manager->GetValueAsInt(valueId, &currentValue)) {
                event.value = static_cast<double>(currentValue);
            } else {
                event.value = std::string{"0"};
            }
        }
        break;
    case OpenZWave::ValueID::ValueType_Decimal:
        {
            float currentValue = 0.0F;
            if (manager->GetValueAsFloat(valueId, &currentValue)) {
                event.value = static_cast<double>(currentValue);
            } else {
                event.value = std::string{"0"};
            }
        }
        break;
    case OpenZWave::ValueID::ValueType_List:
        {
            std::string currentValue{};
            if (manager->GetValueListSelection(valueId, &currentValue)) {
                event.value = currentValue;
            } else {
                event.value = std::string{};
            }
        }
        break;
    case OpenZWave::ValueID::ValueType_String:
    case OpenZWave::ValueID::ValueType_Button:
    case OpenZWave::ValueID::ValueType_Raw:
    case OpenZWave::ValueID::ValueType_BitSet:
    case OpenZWave::ValueID::ValueType_Schedule:
    default:
        {
            std::string currentValue{};
            (void)manager->GetValueAsString(valueId, &currentValue);
            event.value = currentValue;
        }
        break;
    }

    return event;
}

std::string OpenZwaveRuntimeDriverPort::toLower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string OpenZwaveRuntimeDriverPort::valueTypeName(OpenZWave::ValueID const& valueId) {
    switch (valueId.GetType()) {
    case OpenZWave::ValueID::ValueType_Bool:
        return "bool";
    case OpenZWave::ValueID::ValueType_Byte:
        return "byte";
    case OpenZWave::ValueID::ValueType_Decimal:
        return "decimal";
    case OpenZWave::ValueID::ValueType_Int:
        return "int";
    case OpenZWave::ValueID::ValueType_List:
        return "list";
    case OpenZWave::ValueID::ValueType_Schedule:
        return "schedule";
    case OpenZWave::ValueID::ValueType_Short:
        return "short";
    case OpenZWave::ValueID::ValueType_String:
        return "string";
    case OpenZWave::ValueID::ValueType_Button:
        return "button";
    case OpenZWave::ValueID::ValueType_Raw:
        return "raw";
    case OpenZWave::ValueID::ValueType_BitSet:
        return "bitset";
    default:
        return "string";
    }
}

std::string OpenZwaveRuntimeDriverPort::controllerStateText(const std::uint8_t stateCode) {
    switch (stateCode) {
    case OpenZWave::Driver::ControllerState_Normal:
        return "normal";
    case OpenZWave::Driver::ControllerState_Starting:
        return "starting";
    case OpenZWave::Driver::ControllerState_Cancel:
        return "cancel";
    case OpenZWave::Driver::ControllerState_Error:
        return "error";
    case OpenZWave::Driver::ControllerState_Waiting:
        return "waiting";
    case OpenZWave::Driver::ControllerState_Sleeping:
        return "sleeping";
    case OpenZWave::Driver::ControllerState_InProgress:
        return "in-progress";
    case OpenZWave::Driver::ControllerState_Completed:
        return "completed";
    case OpenZWave::Driver::ControllerState_Failed:
        return "failed";
    case OpenZWave::Driver::ControllerState_NodeOK:
        return "node-ok";
    case OpenZWave::Driver::ControllerState_NodeFailed:
        return "node-failed";
    default:
        return "unknown";
    }
}

std::uint32_t OpenZwaveRuntimeDriverPort::requireHomeId() const {
    std::scoped_lock lock{mutex_};
    if (homeId_ == 0U) {
        throw std::runtime_error("OpenZWave driver not ready yet");
    }
    return homeId_;
}

void OpenZwaveRuntimeDriverPort::ensureStarted() {
    std::scoped_lock lock{mutex_};
    if (started_) {
        return;
    }

    if (controllerPath_.empty()) {
        throw std::runtime_error("zwave usbDevice is empty");
    }

    const std::filesystem::path configPath = openzwaveConfigPath();
    const std::filesystem::path userPath = openzwaveUserPath();

    std::filesystem::create_directories(userPath);

    OpenZWave::Options* options = OpenZWave::Options::Get();
    if (options == nullptr) {
        options = OpenZWave::Options::Create(configPath.string(), userPath.string(), "");
        ownsOptions_ = true;
    }

    if (options == nullptr) {
        throw std::runtime_error("failed to create OpenZWave options");
    }

    if (!options->AreLocked()) {
        const bool enableProtocolLogging = logLevel_ > 0U;
        (void)options->AddOptionBool("Logging", enableProtocolLogging);
        (void)options->AddOptionBool("ConsoleOutput", enableProtocolLogging);
        (void)options->AddOptionInt("PollInterval", static_cast<int>(pollIntervalMs_));
        (void)options->AddOptionInt("SaveLogLevel", mapSaveLogLevel(logLevel_));
        (void)options->AddOptionInt("QueueLogLevel", mapQueueLogLevel(logLevel_));
        (void)options->AddOptionInt("DumpTriggerLevel", kOzwLogLevelNone);
    }

    if (!options->AreLocked() && !options->Lock()) {
        throw std::runtime_error("failed to lock OpenZWave options");
    }

    OpenZWave::Manager* manager = OpenZWave::Manager::Get();
    if (manager == nullptr) {
        manager = OpenZWave::Manager::Create();
        ownsManager_ = true;
    }

    if (manager == nullptr) {
        throw std::runtime_error("failed to create OpenZWave manager");
    }

    if (!manager->AddWatcher(&OpenZwaveRuntimeDriverPort::watcherThunk, this)) {
        throw std::runtime_error("failed to register OpenZWave watcher");
    }
    watcherInstalled_ = true;

    if (!manager->AddDriver(controllerPath_)) {
        throw std::runtime_error("failed to add OpenZWave driver for " + controllerPath_);
    }
    driverInstalled_ = true;

    started_ = true;
}

} // namespace yaha
