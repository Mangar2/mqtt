#include "yaha/zwave_controller/zwave_controller_polling.h"

#include "yaha/zwave_controller/zwave_controller_value_utils.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ranges>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>

namespace yaha {
namespace {

constexpr std::uint16_t kZwaveSwitchMultilevelClassId = 0x26U;
constexpr std::uint32_t kPendingCommandLoopSleepMs = 20U;

} // namespace

ZwaveControllerPolling::ZwaveControllerPolling(
    const std::chrono::milliseconds fullDevicePollInterval,
    const std::chrono::milliseconds commandReactionPollInterval,
    const std::chrono::milliseconds commandReactionTimeout,
    RequestNodeStateCallback requestNodeStateCallback,
    TimeoutFeedbackCallback timeoutFeedbackCallback,
    ConfiguredNodeIdsProvider configuredNodeIdsProvider)
    : fullDevicePollInterval_(fullDevicePollInterval)
    , commandReactionPollInterval_(commandReactionPollInterval)
    , commandReactionTimeout_(commandReactionTimeout)
    , requestNodeStateCallback_(std::move(requestNodeStateCallback))
    , timeoutFeedbackCallback_(std::move(timeoutFeedbackCallback))
    , configuredNodeIdsProvider_(std::move(configuredNodeIdsProvider)) {
}

ZwaveControllerPolling::~ZwaveControllerPolling() {
    stop();
}

void ZwaveControllerPolling::start() {
    if (pollThread_.joinable()) {
        return;
    }

    stopRequested_.store(false);
    lastFullDevicePollAt_ = std::chrono::steady_clock::now();
    pollThread_ = std::thread([this] {
        runLoop();
    });
}

void ZwaveControllerPolling::stop() {
    stopRequested_.store(true);
    if (pollThread_.joinable()) {
        pollThread_.join();
    }
}

void ZwaveControllerPolling::rememberPendingCommand(
    const std::string& replyTopic,
    const ZwaveResolvedId& target,
    const Value& expectedValue,
    const ReasonList& reasons) {
    PendingCommand pendingCommand{
        .replyTopic = replyTopic,
        .target = target,
        .expectedValue = expectedValue,
        .reasons = reasons,
        .sentAt = std::chrono::steady_clock::now(),
        .lastPollAt = std::chrono::steady_clock::time_point{}}
    ;

    std::scoped_lock lock{pendingCommandsMutex_};
    auto iterator = pendingCommands_.begin();
    while (iterator != pendingCommands_.end()) {
        const bool sameReplyTopic = iterator->replyTopic == pendingCommand.replyTopic;
        const bool sameTarget = iterator->target.nodeId == pendingCommand.target.nodeId
            && iterator->target.classId == pendingCommand.target.classId
            && iterator->target.instance == pendingCommand.target.instance
            && iterator->target.index == pendingCommand.target.index;
        const bool sameExpectedValue = zwave_controller_value_utils::valuesEquivalent(
            iterator->expectedValue,
            pendingCommand.expectedValue);
        if (sameReplyTopic && sameTarget && sameExpectedValue) {
            iterator = pendingCommands_.erase(iterator);
            continue;
        }
        ++iterator;
    }

    pendingCommands_.push_back(std::move(pendingCommand));
}

ReasonList ZwaveControllerPolling::takeMatchingPendingReasons(
    const std::string& replyTopic,
    const std::uint16_t nodeId,
    const std::uint16_t classId,
    const std::uint8_t instance,
    const std::uint8_t index,
    const Value& outboundValue) {
    std::scoped_lock lock{pendingCommandsMutex_};
    const auto nowValue = std::chrono::steady_clock::now();

    auto iterator = pendingCommands_.begin();
    while (iterator != pendingCommands_.end()) {
        if (nowValue - iterator->sentAt >= commandReactionTimeout_) {
            iterator = pendingCommands_.erase(iterator);
            continue;
        }

        const bool sameReplyTopic = iterator->replyTopic == replyTopic;
        const bool sameTarget = iterator->target.nodeId == nodeId
            && iterator->target.classId == classId
            && iterator->target.instance == instance
            && iterator->target.index == index;
        const auto expectedSemanticBool = zwave_controller_value_utils::valueAsSemanticBool(iterator->expectedValue);
        const auto outboundSemanticBool = zwave_controller_value_utils::valueAsSemanticBool(outboundValue);
        const bool sameExpectedValue = zwave_controller_value_utils::valuesEquivalent(iterator->expectedValue, outboundValue)
            || (classId == kZwaveSwitchMultilevelClassId
                && expectedSemanticBool.has_value()
                && outboundSemanticBool.has_value()
                && *expectedSemanticBool == *outboundSemanticBool);
        if (sameReplyTopic && sameTarget && sameExpectedValue) {
            ReasonList reasons = iterator->reasons;
            pendingCommands_.erase(iterator);
            return reasons;
        }

        ++iterator;
    }

    return {};
}

void ZwaveControllerPolling::cacheTopicState(
    const std::string& topic,
    const Value& value,
    const std::optional<std::uint64_t>& valueId) {
    std::scoped_lock lock{cachedTopicStatesMutex_};
    cachedTopicStates_[topic] = CachedTopicState{.value = value, .valueId = valueId};
}

void ZwaveControllerPolling::removePendingCommandsForNode(const std::uint16_t nodeId) {
    std::scoped_lock lock{pendingCommandsMutex_};
    const auto remainingRange = std::ranges::remove_if(
        pendingCommands_,
        [nodeId](const PendingCommand& pendingCommand) {
            return pendingCommand.target.nodeId == nodeId;
        });
    pendingCommands_.erase(remainingRange.begin(), remainingRange.end());
}

void ZwaveControllerPolling::removeCachedTopics(const std::vector<std::string>& topics) {
    if (topics.empty()) {
        return;
    }

    std::scoped_lock lock{cachedTopicStatesMutex_};
    for (const auto& topic : topics) {
        cachedTopicStates_.erase(topic);
    }
}

std::string ZwaveControllerPolling::describeTimeoutSource(const std::uint16_t nodeId) const {
    std::scoped_lock lock{pendingCommandsMutex_};
    const auto match = std::ranges::find_if(pendingCommands_, [nodeId](const PendingCommand& pendingCommand) {
        return pendingCommand.target.nodeId == nodeId;
    });

    if (match == pendingCommands_.end()) {
        return "source=openzwave_notification_timeout context=no_pending_command";
    }

    return "source=openzwave_notification_timeout context=pending_command topic=" + match->replyTopic
        + " target=node/" + std::to_string(match->target.nodeId)
        + "/class/" + std::to_string(match->target.classId)
        + "/instance/" + std::to_string(match->target.instance)
        + "/index/" + std::to_string(match->target.index)
        + " expected=" + zwave_controller_value_utils::valueToDebugText(match->expectedValue);
}

void ZwaveControllerPolling::runLoop() {
    while (!stopRequested_.load()) {
        try {
            pollPendingCommands();
            pollConfiguredNodes();
        } catch (...) {
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{kPendingCommandLoopSleepMs});
    }
}

void ZwaveControllerPolling::pollPendingCommands() {
    const auto nowValue = std::chrono::steady_clock::now();
    std::unordered_set<std::uint16_t> nodesToPoll{};

    struct TimedOutCommand {
        std::string replyTopic{};
        ReasonList reasons{};
    };
    std::vector<TimedOutCommand> timedOutCommands{};

    {
        std::scoped_lock lock{pendingCommandsMutex_};
        auto iterator = pendingCommands_.begin();
        while (iterator != pendingCommands_.end()) {
            if (nowValue - iterator->sentAt >= commandReactionTimeout_) {
                timedOutCommands.push_back(TimedOutCommand{
                    .replyTopic = iterator->replyTopic,
                    .reasons = iterator->reasons});
                iterator = pendingCommands_.erase(iterator);
                continue;
            }

            if (nowValue - iterator->lastPollAt >= commandReactionPollInterval_) {
                iterator->lastPollAt = nowValue;
                nodesToPoll.insert(iterator->target.nodeId);
            }
            ++iterator;
        }
    }

    for (const auto& timedOutCommand : timedOutCommands) {
        std::optional<CachedTopicState> cachedState{};
        {
            std::scoped_lock lock{cachedTopicStatesMutex_};
            const auto iterator = cachedTopicStates_.find(timedOutCommand.replyTopic);
            if (iterator != cachedTopicStates_.end()) {
                cachedState = iterator->second;
            }
        }

        if (!cachedState.has_value()) {
            continue;
        }

        timeoutFeedbackCallback_(
            timedOutCommand.replyTopic,
            cachedState->value,
            cachedState->valueId,
            timedOutCommand.reasons);
    }

    for (const auto nodeId : nodesToPoll) {
        requestNodeStateCallback_(nodeId);
    }
}

void ZwaveControllerPolling::pollConfiguredNodes() {
    const auto nowValue = std::chrono::steady_clock::now();
    if (nowValue - lastFullDevicePollAt_ < fullDevicePollInterval_) {
        return;
    }
    lastFullDevicePollAt_ = nowValue;

    const std::vector<std::uint16_t> configuredNodeIds = configuredNodeIdsProvider_();
    std::unordered_set<std::uint16_t> deduplicatedNodes{};
    for (const auto nodeId : configuredNodeIds) {
        deduplicatedNodes.insert(nodeId);
    }

    for (const auto nodeId : deduplicatedNodes) {
        requestNodeStateCallback_(nodeId);
    }
}

} // namespace yaha
