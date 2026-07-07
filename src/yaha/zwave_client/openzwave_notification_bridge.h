#pragma once

#include "yaha/zwave_controller/zwave_controller.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace OpenZWave {
class Notification;
class ValueID;
} // namespace OpenZWave

namespace yaha {

class OpenZwaveNotificationBridge final {
public:
    struct DiscoveryState {
        bool hasNode{false};
        bool hasClass{false};
        bool hasInstance{false};
        bool hasIndex{false};
        std::uint8_t genreCode{1U};
    };

    struct PollValueEntry {
        std::uint8_t instance{0U};
        std::uint16_t index{0U};
        std::uint64_t rawValueId{0U};
    };

    OpenZwaveNotificationBridge() = default;

    void bindController(ZwaveController* controller);
    void reset();

    void handleNotification(const OpenZWave::Notification& notification);

    [[nodiscard]] bool isNodeReady(std::uint16_t nodeId) const;
    [[nodiscard]] std::uint32_t requireHomeId() const;
    [[nodiscard]] DiscoveryState snapshotDiscoveryState(const ZwaveResolvedId& target) const;
    [[nodiscard]] std::string discoverySnapshotForLog(std::uint32_t homeId, const ZwaveResolvedId& target) const;

    [[nodiscard]] std::vector<PollValueEntry> collectPollValueEntries(std::uint16_t nodeId, std::uint16_t classId) const;
    [[nodiscard]] std::vector<std::uint16_t> collectKnownNodes() const;
    [[nodiscard]] bool markPollEnabled(std::uint64_t rawValueId);

private:
    using ValueIndexMap = std::unordered_map<std::uint16_t, std::uint64_t>;
    using ValueInstanceMap = std::unordered_map<std::uint8_t, ValueIndexMap>;
    using ValueClassMap = std::unordered_map<std::uint16_t, ValueInstanceMap>;
    using ValueGenreIndexMap = std::unordered_map<std::uint16_t, std::uint8_t>;
    using ValueGenreInstanceMap = std::unordered_map<std::uint8_t, ValueGenreIndexMap>;
    using ValueGenreClassMap = std::unordered_map<std::uint16_t, ValueGenreInstanceMap>;

    void handleValueAddedOrChanged(const OpenZWave::Notification& notification, bool changed);
    void handleValueRemoved(const OpenZWave::Notification& notification);
    void cacheDiscoveredValue(const OpenZWave::ValueID& valueId);
    void eraseDiscoveredValue(const OpenZWave::ValueID& valueId);
    void eraseValueIdCacheUnlocked(const OpenZWave::ValueID& valueId);
    void eraseValueGenreCacheUnlocked(const OpenZWave::ValueID& valueId);

    [[nodiscard]] static ZwaveNodeInfo buildNodeInfo(std::uint32_t homeId, std::uint16_t nodeId);
    [[nodiscard]] static ZwaveControllerValueEvent buildValueEvent(const OpenZWave::ValueID& valueId);
    [[nodiscard]] static std::string valueTypeName(const OpenZWave::ValueID& valueId);
    [[nodiscard]] static std::string controllerStateText(std::uint8_t stateCode);

    mutable std::mutex mutex_{};
    ZwaveController* controller_{nullptr};
    std::uint32_t homeId_{0U};
    std::unordered_set<std::uint16_t> knownNodes_{};
    std::unordered_set<std::uint16_t> readyNodes_{};
    std::unordered_set<std::uint64_t> enabledPollValueIds_{};
    std::unordered_map<std::uint16_t, ValueClassMap> valueIdCache_{};
    std::unordered_map<std::uint16_t, ValueGenreClassMap> valueGenreCache_{};
};

} // namespace yaha
