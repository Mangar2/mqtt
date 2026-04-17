#pragma once

/**
 * @file topic_matcher.h
 * @brief Topic matching against a SubscriptionTrie (Module 3.3).
 */

#include <string>
#include <string_view>
#include <vector>

#include "data_model/subscription/subscription.h"
#include "topic/trie/subscription_trie.h"

namespace mqtt {

/**
 * @brief A (client_id, Subscription) pair returned by TopicMatcher::match.
 */
struct MatchResult {
  std::string client_id;     ///< Identifier of the subscribing client.
  Subscription subscription; ///< The matching subscription.
};

/**
 * @brief Matches a publish topic name against all subscriptions in a
 * SubscriptionTrie.
 *
 * Implements MQTT 5.0 Section 4.7 topic matching rules:
 * - Exact level match (3.3.1).
 * - Single-level wildcard `+` matches exactly one topic level (3.3.2).
 * - Multi-level wildcard `#` matches zero or more remaining levels (3.3.3).
 * - Topics beginning with `$` are excluded from wildcard-only filters at the
 *   first level (MQTT 5.0 Section 4.7.2) (3.3.4).
 */
class TopicMatcher {
public:
  /**
   * @brief Finds all subscriptions that match the given publish topic name.
   *
   * Traverses the trie using exact matching, `+` wildcard matching, and `#`
   * wildcard matching.  System topics (those whose name begins with `$`) are
   * never matched by a filter whose first level is a wildcard.
   *
   * @param trie       The subscription trie to search.
   * @param topic_name The publish topic name (must not contain wildcards).
   * @return Vector of all matching (client_id, Subscription) pairs.
   *         May contain multiple entries for the same client when subscriptions
   *         overlap.
   */
  [[nodiscard]] static std::vector<MatchResult>
  match(const SubscriptionTrie &trie, std::string_view topic_name);

private:
  /**
   * @brief Recursive trie traversal helper.
   *
   * @param node      Current trie node being examined.
   * @param levels    All topic levels split from the publish topic name.
   * @param depth     Index of the next unconsumed level in @p levels.
   * @param is_system True when the topic name begins with `$`.
   * @param results   Accumulator for matching (client_id, Subscription) pairs.
   */
  static void collect_matches(const SubscriptionTrie::Node &node,
                              const std::vector<std::string_view> &levels,
                              std::size_t depth, bool is_system,
                              std::vector<MatchResult> &results);
};

} // namespace mqtt
