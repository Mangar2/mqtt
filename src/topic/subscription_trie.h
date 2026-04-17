#pragma once

/**
 * @file subscription_trie.h
 * @brief Trie-based storage for MQTT 5.0 subscriptions (Module 3.2).
 *
 * Moved from topic/trie/ — source files must not be nested inside another
 * source directory (see code-create skill).
 */

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "data_model/subscription/subscription.h"

namespace mqtt {

/**
 * @brief Trie data structure for storing MQTT 5.0 topic subscriptions.
 *
 * Each node in the trie represents one topic level (the segment between two
 * '/' separators).  Subscriptions are stored at the node that corresponds to
 * the last level of their topic filter.
 *
 * Wildcard levels (`"+"` and `"#"`) are stored as ordinary strings; wildcard
 * semantics during lookup are the responsibility of the topic matcher
 * (Module 3.3).
 *
 * Thread safety: none — external synchronisation required for concurrent use.
 */
class SubscriptionTrie {
public:
  /**
   * @brief Inserts or replaces a subscription for a client.
   *
   * Splits `sub.topic_filter.value` on `'/'` and descends (creating nodes
   * as needed) to the terminal node.  If @p client_id already has a
   * subscription stored at that node it is overwritten.
   *
   * @param client_id Identifier of the subscribing client.
   * @param sub       Subscription to store; must carry a valid topic filter.
   */
  void insert(std::string_view client_id, const Subscription &sub);

  /**
   * @brief Removes a specific subscription for a client.
   *
   * Follows the exact level path derived from @p topic_filter.  Erases the
   * @p client_id entry at the terminal node.  Any nodes that become empty
   * (no subscriptions and no children) after the removal are pruned.
   *
   * Does nothing when no matching subscription exists.
   *
   * @param client_id    Identifier of the subscribing client.
   * @param topic_filter Topic filter identifying the subscription to remove.
   */
  void remove(std::string_view client_id, std::string_view topic_filter);

  /**
   * @brief Removes all subscriptions for a client.
   *
   * Performs a depth-first traversal of the whole trie and erases every
   * entry keyed by @p client_id.  Empty nodes are pruned after each visit.
   *
   * Does nothing when @p client_id has no subscriptions.
   *
   * @param client_id Identifier of the client whose subscriptions are cleared.
   */
  void remove_all(std::string_view client_id);

  /**
   * @brief Returns the total number of stored (client, filter) pairs.
   * @return Count of all subscriptions across all trie nodes.
   */
  [[nodiscard]] std::size_t size() const noexcept;

  /**
   * @brief A single trie node representing one topic level.
   *
   * Exposed publicly to allow the Topic Matcher (Module 3.3) to traverse
   * the trie without requiring friend-class access.
   */
  struct Node {
    std::unordered_map<std::string, std::unique_ptr<Node>>
        children; ///< Child nodes keyed by topic-level string.
    std::unordered_map<std::string, Subscription>
        subscriptions; ///< Subscriptions keyed by client_id.

    /** @brief Returns true when this node carries no data and has no children.
     */
    [[nodiscard]] bool empty() const noexcept {
      return children.empty() && subscriptions.empty();
    }
  };

  /**
   * @brief Provides read-only access to the root node for trie traversal.
   *
   * Intended for use by the Topic Matcher (Module 3.3).
   * The root node is a sentinel; it never holds subscriptions directly.
   *
   * @return Const reference to the root node.
   */
  [[nodiscard]] const Node &root() const noexcept { return *root_; }

private:
  std::unique_ptr<Node> root_{std::make_unique<Node>()};

  /** @brief Splits @p filter on '/' and returns the resulting level strings. */
  [[nodiscard]] static std::vector<std::string>
  split_filter(std::string_view filter);

  /**
   * @brief Recursive helper for remove().
   * @return true when @p node is empty after the operation (caller should prune
   * it).
   */
  static bool remove_recursive(Node &node,
                               const std::vector<std::string> &levels,
                               std::size_t depth, std::string_view client_id);

  /**
   * @brief Recursive helper for remove_all().
   *        Removes all entries for @p client_id and prunes empty children.
   */
  static void remove_all_recursive(Node &node, std::string_view client_id);

  /** @brief Recursive helper for size(). */
  [[nodiscard]] static std::size_t count_recursive(const Node &node) noexcept;
};

} // namespace mqtt
