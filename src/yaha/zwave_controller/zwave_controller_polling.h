#pragma once

#include "yaha/message/message.h"
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
#include <vector>

namespace yaha {

class ZwaveControllerPolling final {
public:
    using RequestNodeStateCallback = std::function<void(std::uint16_t)>;
    using TimeoutFeedbackCallback = std::function<void(
        const std::string&,
        const Value&,
        const std::optional<std::uint64_t>&,
        const ReasonList&)>;
    using ConfiguredNodeIdsProvider = std::function<std::vector<std::uint16_t>()>;

    ZwaveControllerPolling(
        std::chrono::milliseconds fullDevicePollInterval,
        std::chrono::milliseconds commandReactionPollInterval,
        std::chrono::milliseconds commandReactionTimeout,
        RequestNodeStateCallback requestNodeStateCallback,
        TimeoutFeedbackCallback timeoutFeedbackCallback,
        ConfiguredNodeIdsProvider configuredNodeIdsProvider);

    ~ZwaveControllerPolling();

    ZwaveControllerPolling(const ZwaveControllerPolling&) = delete;
    ZwaveControllerPolling& operator=(const ZwaveControllerPolling&) = delete;

    void start();
    void stop();

    void rememberPendingCommand(
        const std::string& replyTopic,
        const ZwaveResolvedId& target,
        const Value& expectedValue,
        const ReasonList& reasons);

    [[nodiscard]] ReasonList takeMatchingPendingReasons(
        const std::string& replyTopic,
        std::uint16_t nodeId,
        std::uint16_t classId,
        std::uint8_t instance,
        std::uint8_t index,
        const Value& outboundValue);

    void cacheTopicState(const std::string& topic, const Value& value, const std::optional<std::uint64_t>& valueId);
    void removePendingCommandsForNode(std::uint16_t nodeId);
    void removeCachedTopics(const std::vector<std::string>& topics);

    [[nodiscard]] std::string describeTimeoutSource(std::uint16_t nodeId) const;

private:
    struct PendingCommand {
        std::string replyTopic{};
        ZwaveResolvedId target{};
        Value expectedValue{std::string{}};
        ReasonList reasons{};
        std::chrono::steady_clock::time_point sentAt{};
        std::chrono::steady_clock::time_point lastPollAt{};
    };

    struct CachedTopicState {
        Value value{std::string{}};
        std::optional<std::uint64_t> valueId{};
    };

    void runLoop();
    void pollPendingCommands();
    void pollConfiguredNodes();

    std::chrono::milliseconds fullDevicePollInterval_{};
    std::chrono::milliseconds commandReactionPollInterval_{};
    std::chrono::milliseconds commandReactionTimeout_{};
    RequestNodeStateCallback requestNodeStateCallback_{};
    TimeoutFeedbackCallback timeoutFeedbackCallback_{};
    ConfiguredNodeIdsProvider configuredNodeIdsProvider_{};

    mutable std::mutex pendingCommandsMutex_{};
    std::vector<PendingCommand> pendingCommands_{};

    mutable std::mutex cachedTopicStatesMutex_{};
    std::unordered_map<std::string, CachedTopicState> cachedTopicStates_{};

    std::thread pollThread_{};
    std::atomic_bool stopRequested_{false};
    std::chrono::steady_clock::time_point lastFullDevicePollAt_{std::chrono::steady_clock::now()};
};

} // namespace yaha
