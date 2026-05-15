#pragma once

/**
 * @file rule_runtime_engine.h
 * @brief Runtime rule gating and delivery-control helpers for automation client.
 */

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "yaha/automation/expression_evaluator.h"
#include "yaha/automation/rules_tree_parser.h"
#include "yaha/message/message.h"

namespace yaha {

/**
 * @brief One stored motion event used by event-gate evaluation.
 */
struct MotionEventRecord {
    std::string topicName;                                                ///< Topic name of the motion event.
    std::chrono::system_clock::time_point timestamp;                      ///< Event timestamp.
    std::uint64_t sequenceNumber{0U};                                     ///< Monotonic insertion order identifier.
};

/**
 * @brief Runtime event history state for rule gating.
 */
struct RuleRuntimeEventState {
    std::vector<MotionEventRecord> motionEvents;                          ///< Ordered motion event history.
    std::set<std::string> nonMotionEvents;                                ///< One-cycle non-motion event topics.
    std::uint64_t nextSequenceNumber{1U};                                 ///< Next motion event sequence number.
};

/**
 * @brief Runtime delivery-control state for dedup, delay, and cooldown.
 */
struct RuleRuntimeDeliveryState {
    /**
     * @brief Per-output-key suppression and timing state.
     */
    struct OutputState {
        std::string candidateHash;                                        ///< Last observed candidate topic/value hash.
        std::chrono::system_clock::time_point candidateSince;             ///< First-seen timestamp for current candidate hash.
        std::optional<std::string> emittedHash;                           ///< Last emitted candidate hash.
        std::optional<std::chrono::system_clock::time_point> emittedAt;   ///< Timestamp of last emission.
    };

    std::map<std::string, OutputState> outputStatesByKey;                 ///< State map keyed by rulePath + topic.
};

/**
 * @brief Result of evaluating rules with runtime gates and delivery controls.
 */
struct RuleRuntimeProcessingResult {
    bool success{true};                                                   ///< True when no processing errors occurred.
    std::size_t processedRules{0U};                                       ///< Count of evaluated rules.
    std::size_t triggeredRules{0U};                                       ///< Count of rules that triggered before delivery suppression.
    std::vector<Message> messages;                                        ///< Outbound messages after all gates and suppression.
    std::set<std::string> usedVariables;                                  ///< Aggregated used variables from executed scripts.
    std::vector<std::string> errors;                                      ///< Path-aware runtime processing errors.
};

/**
 * @brief Supports runtime rule validation, event ingestion, and gated processing.
 */
class RuleRuntimeEngine {
public:
    /**
     * @brief Validate the minimum management rule shape for update commands.
     *
     * @param ruleNode Parsed rule node from management payload.
     * @return True when rule shape is valid for runtime insertion.
     */
    [[nodiscard]] static bool isRuleNodeStructureValid(const RuleTreeNode& ruleNode);

    /**
     * @brief Ingest one domain message into event history state.
     *
     * @param message Incoming domain message.
     * @param messageTime Timestamp associated with this message.
     * @param motionTopicFilters Configured motion topic filters.
     * @param eventState Mutable runtime event state.
     */
    static void ingestDomainMessageEvent(
        const Message& message,
        const std::chrono::system_clock::time_point& messageTime,
        const std::vector<std::string>& motionTopicFilters,
        RuleRuntimeEventState* eventState);

    /**
     * @brief Evaluate full rule tree with event/time/delivery gates.
     *
     * @param rulesRoot Full rules tree root node.
     * @param variables Runtime variable snapshot including internal variables.
     * @param evaluationTime Timestamp used for time-based gates.
     * @param eventState Mutable runtime event history.
     * @param deliveryState Mutable runtime delivery-control state.
     * @return Aggregated runtime processing result.
     */
    [[nodiscard]] static RuleRuntimeProcessingResult processRules(
        const RuleTreeNode& rulesRoot,
        const ExpressionEvaluator::VariableMap& variables,
        const std::chrono::system_clock::time_point& evaluationTime,
        RuleRuntimeEventState* eventState,
        RuleRuntimeDeliveryState* deliveryState);

    /**
     * @brief Clear one-cycle non-motion events after a processing cycle.
     *
     * @param eventState Mutable runtime event state.
     */
    static void clearNonMotionEvents(RuleRuntimeEventState* eventState);
};

} // namespace yaha
