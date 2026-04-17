#pragma once

/**
 * @file shared_subscription_dispatcher.h
 * @brief SharedSubscriptionDispatcher — round-robin delivery for MQTT 5.0
 *        shared subscriptions (Module 12.5).
 */

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "data_model/subscription/subscription.h"
#include "topic/topic_matcher.h"

namespace mqtt {

/**
 * @brief Manages MQTT 5.0 shared subscription groups and selects one delivery
 *        target per group for each incoming message (Module 12.5).
 *
 * Shared subscriptions follow the `$share/<group>/<topic_filter>` syntax
 * (MQTT 5.0 §4.8.2).  Multiple clients registered in the same group share
 * message delivery so that each message is forwarded to exactly one member:
 *
 * - add_member() registers a client as a subscriber within a named group
 *   (12.5.1).
 * - select_next_for_topic() returns one MatchResult per group whose stored
 *   filter matches the publish topic, cycling through members via round-robin
 *   (12.5.2).
 * - remove_member() deregisters a client; a group with no remaining members
 *   is cleaned up automatically (12.5.3).
 *
 * Topic filter matching follows MQTT 5.0 §4.7 semantics: `+` matches exactly
 * one level, `#` matches any remaining levels, and topics beginning with `$`
 * are excluded from filters whose first level is a wildcard.
 *
 * Thread safety: none — external synchronisation required.
 */
class SharedSubscriptionDispatcher {
public:
  /**
   * @brief Register a client as a member of a shared subscription group
   *        (12.5.1).
   *
   * If the client is already registered for the same (group, topic_filter)
   * pair its subscription entry is replaced.
   *
   * @param group        Share group name (the segment after `$share/`).
   * @param topic_filter Underlying topic filter (the segment after the group
   *                     name).
   * @param client_id    Identifier of the subscribing client.
   * @param sub          Subscription options for this member.
   */
  void add_member(std::string_view group, std::string_view topic_filter,
                  std::string_view client_id, const Subscription &sub);

  /**
   * @brief Remove a client from a shared subscription group (12.5.3).
   *
   * Does nothing when the client is not a member.  Removes the group entry
   * entirely when no members remain after the removal.
   *
   * @param group        Share group name.
   * @param topic_filter Underlying topic filter.
   * @param client_id    Identifier of the client to remove.
   */
  void remove_member(std::string_view group, std::string_view topic_filter,
                     std::string_view client_id);

  /**
   * @brief Remove all shared subscription memberships held by a client.
   *
   * Iterates all groups and removes the client from each entry it belongs
   * to.  Groups that become empty are cleaned up.
   *
   * @param client_id Identifier of the client to remove globally.
   */
  void remove_client(std::string_view client_id);

  /**
   * @brief Select one delivery target per matching group for a publish topic
   *        (12.5.2).
   *
   * For each registered (group, filter) pair whose filter matches
   * @p topic_name, the next member is selected via round-robin and returned
   * as a MatchResult.  The round-robin counter is advanced on each call.
   *
   * @param topic_name Publish topic name (must not contain wildcards).
   * @return One MatchResult per matching group; empty when no group matches.
   */
  [[nodiscard]] std::vector<MatchResult>
  select_next_for_topic(std::string_view topic_name);

  /**
   * @brief Return the number of members in a specific group.
   *
   * @param group        Share group name.
   * @param topic_filter Underlying topic filter.
   * @return Member count; 0 when the group does not exist.
   */
  [[nodiscard]] std::size_t
  member_count(std::string_view group,
               std::string_view topic_filter) const noexcept;

private:
  /**
   * @brief One member's registration within a shared subscription group.
   */
  struct MemberEntry {
    std::string client_id;     ///< Client identifier.
    Subscription subscription; ///< Subscription options for this member.
  };

  /**
   * @brief State for one (group, topic_filter) combination.
   */
  struct GroupState {
    std::vector<MemberEntry>
        members;              ///< Registered members in insertion order.
    std::size_t next_idx{0U}; ///< Round-robin cursor.
  };

  /// Composite key: group name + '\0' + topic_filter (guaranteed unique).
  [[nodiscard]] static std::string make_key(std::string_view group,
                                            std::string_view topic_filter);

  /**
   * @brief Check whether a publish topic matches an MQTT topic filter.
   *
   * Follows MQTT 5.0 §4.7 semantics including `$`-topic system exclusion.
   *
   * @param topic_name   Publish topic name (no wildcards).
   * @param topic_filter Subscription filter; may contain `+` and `#`.
   * @return `true` when the filter matches the topic name.
   */
  [[nodiscard]] static bool matches_filter(std::string_view topic_name,
                                           std::string_view topic_filter);

  /**
   * @brief Recursive level-by-level matcher used by matches_filter.
   *
   * @param topic    Remaining topic string from the current level onward.
   * @param filter   Remaining filter string from the current level onward.
   * @param is_root  True only at the first recursive call (top-level `$`
   * check).
   * @return `true` when the remaining filter covers the remaining topic.
   */
  [[nodiscard]] static bool match_recursive(std::string_view topic,
                                            std::string_view filter,
                                            bool is_root);

  std::unordered_map<std::string, GroupState>
      groups_; ///< (group+filter) → state.
};

} // namespace mqtt
