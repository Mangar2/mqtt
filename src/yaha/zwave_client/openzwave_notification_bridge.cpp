#include "yaha/zwave_client/openzwave_notification_bridge.h"

#include "Driver.h"
#include "Manager.h"
#include "Notification.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace yaha {
namespace {

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

[[nodiscard]] OpenZWave::ValueID::ValueGenre decodeGenreOrDefault(const std::uint8_t cachedGenreCode) {
    switch (cachedGenreCode) {
    case 0U:
        return OpenZWave::ValueID::ValueGenre_Basic;
    case 1U:
        return OpenZWave::ValueID::ValueGenre_User;
    case 2U:
        return OpenZWave::ValueID::ValueGenre_Config;
    case 3U:
        return OpenZWave::ValueID::ValueGenre_System;
    default:
        return OpenZWave::ValueID::ValueGenre_User;
    }
}

[[nodiscard]] std::string genreNameForLog(const OpenZWave::ValueID::ValueGenre genre) {
    switch (genre) {
    case OpenZWave::ValueID::ValueGenre_Basic:
        return "basic";
    case OpenZWave::ValueID::ValueGenre_User:
        return "user";
    case OpenZWave::ValueID::ValueGenre_Config:
        return "config";
    case OpenZWave::ValueID::ValueGenre_System:
        return "system";
    default:
        return "unknown";
    }
}

} // namespace

void OpenZwaveNotificationBridge::bindController(ZwaveController* controller) {
    std::scoped_lock lock{mutex_};
    controller_ = controller;
}

void OpenZwaveNotificationBridge::reset() {
    std::scoped_lock lock{mutex_};
    homeId_ = 0U;
    knownNodes_.clear();
    readyNodes_.clear();
    valueIdCache_.clear();
    valueGenreCache_.clear();
    enabledPollValueIds_.clear();
}

void OpenZwaveNotificationBridge::handleNotification(const OpenZWave::Notification& notification) {
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
    case OpenZWave::Notification::Type_NodeRemoved:
        {
            std::scoped_lock lock{mutex_};
            const auto nodeIterator = valueIdCache_.find(nodeId);
            if (nodeIterator != valueIdCache_.end()) {
                for (const auto& [classId, valuesByInstance] : nodeIterator->second) {
                    (void)classId;
                    for (const auto& [instance, valuesByIndex] : valuesByInstance) {
                        (void)instance;
                        for (const auto& [index, rawValueId] : valuesByIndex) {
                            (void)index;
                            enabledPollValueIds_.erase(rawValueId);
                        }
                    }
                }
            }
            knownNodes_.erase(nodeId);
            readyNodes_.erase(nodeId);
            valueIdCache_.erase(nodeId);
            valueGenreCache_.erase(nodeId);
        }
        controller->onNodeRemoved(nodeId);
        return;
    case OpenZWave::Notification::Type_NodeQueriesComplete:
        {
            std::scoped_lock lock{mutex_};
            readyNodes_.insert(nodeId);
        }
        controller->onNodeReady(nodeId, buildNodeInfo(notification.GetHomeId(), nodeId), "queries_complete");
        return;
    case OpenZWave::Notification::Type_EssentialNodeQueriesComplete:
        {
            std::scoped_lock lock{mutex_};
            readyNodes_.insert(nodeId);
        }
        controller->onNodeReady(nodeId, buildNodeInfo(notification.GetHomeId(), nodeId), "essential_queries_complete");
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
                nodeId,
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

bool OpenZwaveNotificationBridge::isNodeReady(const std::uint16_t nodeId) const {
    std::scoped_lock lock{mutex_};
    return readyNodes_.contains(nodeId);
}

std::uint32_t OpenZwaveNotificationBridge::requireHomeId() const {
    std::scoped_lock lock{mutex_};
    if (homeId_ == 0U) {
        throw std::runtime_error("OpenZWave driver not ready yet");
    }
    return homeId_;
}

OpenZwaveNotificationBridge::DiscoveryState OpenZwaveNotificationBridge::snapshotDiscoveryState(
    const ZwaveResolvedId& target) const {
    DiscoveryState state{};
    std::scoped_lock lock{mutex_};

    if (const auto nodeIterator = valueIdCache_.find(target.nodeId); nodeIterator != valueIdCache_.end()) {
        state.hasNode = true;
        if (const auto classIterator = nodeIterator->second.find(target.classId);
            classIterator != nodeIterator->second.end()) {
            state.hasClass = true;
            if (const auto instanceIterator = classIterator->second.find(requireUint8(target.instance, "instance"));
                instanceIterator != classIterator->second.end()) {
                state.hasInstance = true;
                state.hasIndex = instanceIterator->second.contains(target.index);
            }
        }
    }

    if (const auto genreNodeIterator = valueGenreCache_.find(target.nodeId);
        genreNodeIterator != valueGenreCache_.end()) {
        if (const auto genreClassIterator = genreNodeIterator->second.find(target.classId);
            genreClassIterator != genreNodeIterator->second.end()) {
            if (const auto genreInstanceIterator = genreClassIterator->second.find(requireUint8(target.instance, "instance"));
                genreInstanceIterator != genreClassIterator->second.end()) {
                if (const auto genreIndexIterator = genreInstanceIterator->second.find(target.index);
                    genreIndexIterator != genreInstanceIterator->second.end()) {
                    state.genreCode = genreIndexIterator->second;
                }
            }
        }
    }

    return state;
}

std::string OpenZwaveNotificationBridge::discoverySnapshotForLog(
    const std::uint32_t homeId,
    const ZwaveResolvedId& target) const {
    std::vector<std::string> entries{};
    std::scoped_lock lock{mutex_};

    const auto nodeIterator = valueIdCache_.find(target.nodeId);
    if (nodeIterator == valueIdCache_.end()) {
        return "none";
    }

    const auto classIterator = nodeIterator->second.find(target.classId);
    if (classIterator == nodeIterator->second.end()) {
        return "none";
    }

    const ValueGenreInstanceMap* genreByInstance = nullptr;
    if (const auto genreNodeIterator = valueGenreCache_.find(target.nodeId);
        genreNodeIterator != valueGenreCache_.end()) {
        const auto genreClassIterator = genreNodeIterator->second.find(target.classId);
        if (genreClassIterator != genreNodeIterator->second.end()) {
            genreByInstance = &genreClassIterator->second;
        }
    }

    for (const auto& [instance, valueByIndex] : classIterator->second) {
        for (const auto& [index, rawValueId] : valueByIndex) {
            const OpenZWave::ValueID discoveredValueId{homeId, rawValueId};
            std::uint8_t genreCode = 1U;

            if (genreByInstance != nullptr) {
                const auto genreInstanceIterator = genreByInstance->find(instance);
                if (genreInstanceIterator != genreByInstance->end()) {
                    const auto genreIndexIterator = genreInstanceIterator->second.find(index);
                    if (genreIndexIterator != genreInstanceIterator->second.end()) {
                        genreCode = genreIndexIterator->second;
                    }
                }
            }

            std::ostringstream entry{};
            entry << "instance=" << static_cast<std::uint32_t>(instance)
                  << ",index=" << index
                  << ",genre=" << genreNameForLog(decodeGenreOrDefault(genreCode))
                  << ",type=" << valueTypeName(discoveredValueId)
                  << ",rawValueId=0x" << std::hex << rawValueId << std::dec;
            entries.push_back(entry.str());
        }
    }

    if (entries.empty()) {
        return "none";
    }

    std::ranges::sort(entries);

    std::ostringstream joined{};
    joined << '[';
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (index > 0U) {
            joined << ';';
        }
        joined << entries[index];
    }
    joined << ']';
    return joined.str();
}

std::vector<OpenZwaveNotificationBridge::PollValueEntry> OpenZwaveNotificationBridge::collectPollValueEntries(
    const std::uint16_t nodeId,
    const std::uint16_t classId) const {
    std::vector<PollValueEntry> entries{};
    std::scoped_lock lock{mutex_};

    const auto nodeIterator = valueIdCache_.find(nodeId);
    if (nodeIterator == valueIdCache_.end()) {
        return entries;
    }

    const auto classIterator = nodeIterator->second.find(classId);
    if (classIterator == nodeIterator->second.end()) {
        return entries;
    }

    for (const auto& [instance, valuesByIndex] : classIterator->second) {
        for (const auto& [index, rawValueId] : valuesByIndex) {
            entries.push_back(PollValueEntry{.instance = instance, .index = index, .rawValueId = rawValueId});
        }
    }

    return entries;
}

std::vector<std::uint16_t> OpenZwaveNotificationBridge::collectKnownNodes() const {
    std::vector<std::uint16_t> nodes{};
    std::scoped_lock lock{mutex_};
    nodes.reserve(knownNodes_.size());
    for (const std::uint16_t nodeId : knownNodes_) {
        nodes.push_back(nodeId);
    }
    return nodes;
}

bool OpenZwaveNotificationBridge::markPollEnabled(const std::uint64_t rawValueId) {
    std::scoped_lock lock{mutex_};
    return enabledPollValueIds_.insert(rawValueId).second;
}

void OpenZwaveNotificationBridge::handleValueAddedOrChanged(
    const OpenZWave::Notification& notification,
    const bool changed) {
    const OpenZWave::ValueID valueId = notification.GetValueID();
    cacheDiscoveredValue(valueId);

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

void OpenZwaveNotificationBridge::handleValueRemoved(const OpenZWave::Notification& notification) {
    const OpenZWave::ValueID valueId = notification.GetValueID();
    eraseDiscoveredValue(valueId);

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

void OpenZwaveNotificationBridge::cacheDiscoveredValue(const OpenZWave::ValueID& valueId) {
    std::scoped_lock lock{mutex_};
    knownNodes_.insert(valueId.GetNodeId());
    valueIdCache_[valueId.GetNodeId()][valueId.GetCommandClassId()][valueId.GetInstance()][valueId.GetIndex()] =
        valueId.GetId();
    valueGenreCache_[valueId.GetNodeId()][valueId.GetCommandClassId()][valueId.GetInstance()][valueId.GetIndex()] =
        static_cast<std::uint8_t>(valueId.GetGenre());
}

void OpenZwaveNotificationBridge::eraseDiscoveredValue(const OpenZWave::ValueID& valueId) {
    std::scoped_lock lock{mutex_};
    eraseValueIdCacheUnlocked(valueId);
    eraseValueGenreCacheUnlocked(valueId);
}

void OpenZwaveNotificationBridge::eraseValueIdCacheUnlocked(const OpenZWave::ValueID& valueId) {
    auto nodeIterator = valueIdCache_.find(valueId.GetNodeId());
    if (nodeIterator != valueIdCache_.end()) {
        auto classIterator = nodeIterator->second.find(valueId.GetCommandClassId());
        if (classIterator != nodeIterator->second.end()) {
            auto instanceIterator = classIterator->second.find(valueId.GetInstance());
            if (instanceIterator != classIterator->second.end()) {
                const auto valueIterator = instanceIterator->second.find(valueId.GetIndex());
                if (valueIterator != instanceIterator->second.end()) {
                    enabledPollValueIds_.erase(valueIterator->second);
                    instanceIterator->second.erase(valueIterator);
                }
                if (instanceIterator->second.empty()) {
                    classIterator->second.erase(instanceIterator);
                }
            }
            if (classIterator->second.empty()) {
                nodeIterator->second.erase(classIterator);
            }
        }
        if (nodeIterator->second.empty()) {
            valueIdCache_.erase(nodeIterator);
            knownNodes_.erase(valueId.GetNodeId());
        }
    }
}

void OpenZwaveNotificationBridge::eraseValueGenreCacheUnlocked(const OpenZWave::ValueID& valueId) {
    auto genreNodeIterator = valueGenreCache_.find(valueId.GetNodeId());
    if (genreNodeIterator != valueGenreCache_.end()) {
        auto genreClassIterator = genreNodeIterator->second.find(valueId.GetCommandClassId());
        if (genreClassIterator != genreNodeIterator->second.end()) {
            auto genreInstanceIterator = genreClassIterator->second.find(valueId.GetInstance());
            if (genreInstanceIterator != genreClassIterator->second.end()) {
                genreInstanceIterator->second.erase(valueId.GetIndex());
                if (genreInstanceIterator->second.empty()) {
                    genreClassIterator->second.erase(genreInstanceIterator);
                }
            }
            if (genreClassIterator->second.empty()) {
                genreNodeIterator->second.erase(genreClassIterator);
            }
        }
        if (genreNodeIterator->second.empty()) {
            valueGenreCache_.erase(genreNodeIterator);
        }
    }
}

ZwaveNodeInfo OpenZwaveNotificationBridge::buildNodeInfo(const std::uint32_t homeId, const std::uint16_t nodeId) {
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

ZwaveControllerValueEvent OpenZwaveNotificationBridge::buildValueEvent(const OpenZWave::ValueID& valueId) {
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

std::string OpenZwaveNotificationBridge::valueTypeName(const OpenZWave::ValueID& valueId) {
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

std::string OpenZwaveNotificationBridge::controllerStateText(const std::uint8_t stateCode) {
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

} // namespace yaha
