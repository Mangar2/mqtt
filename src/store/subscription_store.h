#pragma once

/**
 * @file subscription_store.h
 * @brief In-memory subscription store (Module 4.1).
 */

#include <cstddef>
#include <shared_mutex>
#include <string_view>
#include <vector>

#include "data_model/subscription/subscription.h"
#include "topic/subscription_trie.h"
#include "topic/topic_matcher.h"

namespace mqtt {

/**
 * @brief In-memory store for all active MQTT 5.0 subscriptions (Module 4.1).
 *
 * Wraps `SubscriptionTrie` (Module 3.2) and delegates topic matching to
 * `TopicMatcher` (Module 3.3).  A single instance is shared across all
 * connected clients; external synchronisation is required for concurrent use.
 */
class SubscriptionStore {
public:
  /**
   * @brief Insert or overwrite one subscription for a client (4.1.1).
   *
   * If @p client_id already holds a subscription for the same topic filter
   * it is replaced with @p sub.
   *
   * @param client_id Identifier of the subscribing client.
   * @param sub       Subscription to store; must carry a valid topic filter.
   * @return `true` when this call created a new subscription entry;
   *         `false` when an existing entry was updated.
   */
  bool store(std::string_view client_id, const Subscription &sub);

  /**
   * @brief Remove the named subscription for a client (4.1.2).
   *
   * @param client_id    Identifier of the subscribing client.
   * @param topic_filter Exact filter string of the subscription to remove.
   * @return `true` when a subscription was found and removed; `false` when no
   *         matching entry existed.
   */
  bool remove(std::string_view client_id, std::string_view topic_filter);

  /**
   * @brief Return all subscribers whose filter matches a publish topic (4.1.3).
   *
   * Delegates to `TopicMatcher::match`; supports exact, `+`, `#`, and
   * system-topic exclusion rules (MQTT 5.0 §4.7).
   *
   * @param topic_name The publish topic name (must not contain wildcards).
   * @return Vector of `MatchResult` pairs; may be empty if no subscriptions
   * match.
   */
  [[nodiscard]] std::vector<MatchResult>
  subscribers_for(std::string_view topic_name) const;

  /**
   * @brief Remove all subscriptions held by a client session (4.1.4).
   *
   * Does nothing when @p client_id has no subscriptions.
   *
   * @param client_id Identifier of the client whose subscriptions are cleared.
   */
  void remove_session(std::string_view client_id);

  /**
   * @brief Return the total number of stored (client_id, filter) pairs.
   * @return Count of all subscriptions across all clients.
   */
  [[nodiscard]] std::size_t size() const noexcept;

private:
  mutable std::shared_mutex mutex_;
  SubscriptionTrie trie_; ///< Underlying trie used for storage and traversal.
};

} // namespace mqtt
