#include "topic/trie/subscription_trie.h"

#include <string>
#include <vector>

namespace mqtt {

// ── anonymous helpers
// ──────────────────────────────────────────────────────────

namespace {

/**
 * Splits @p filter on '/' and returns the non-owning string segments
 * as a vector of std::string (owned copies, safe for storage in the trie).
 */
std::vector<std::string> split_by_slash(std::string_view filter) {
  std::vector<std::string> levels;
  std::size_t start = 0;
  while (start <= filter.size()) {
    const auto pos = filter.find('/', start);
    if (pos == std::string_view::npos) {
      levels.emplace_back(filter.substr(start));
      break;
    }
    levels.emplace_back(filter.substr(start, pos - start));
    start = pos + 1;
  }
  return levels;
}

} // namespace

// ── SubscriptionTrie
// ───────────────────────────────────────────────────────────

std::vector<std::string>
SubscriptionTrie::split_filter(std::string_view filter) {
  return split_by_slash(filter);
}

void SubscriptionTrie::insert(std::string_view client_id,
                              const Subscription &sub) {
  const auto levels = split_filter(sub.topic_filter.value);
  Node *cur = root_.get();
  for (const auto &lvl : levels) {
    auto &child = cur->children[lvl];
    if (!child) {
      child = std::make_unique<Node>();
    }
    cur = child.get();
  }
  cur->subscriptions[std::string{client_id}] = sub;
}

bool SubscriptionTrie::remove_recursive(Node &node,
                                        const std::vector<std::string> &levels,
                                        std::size_t depth,
                                        std::string_view client_id) {
  if (depth == levels.size()) {
    node.subscriptions.erase(std::string{client_id});
    return node.empty();
  }

  const auto &lvl = levels[depth];
  auto it = node.children.find(lvl);
  if (it == node.children.end()) {
    return node.empty();
  }

  const bool child_empty =
      remove_recursive(*it->second, levels, depth + 1, client_id);
  if (child_empty) {
    node.children.erase(it);
  }
  return node.empty();
}

void SubscriptionTrie::remove(std::string_view client_id,
                              std::string_view topic_filter) {
  const auto levels = split_filter(topic_filter);
  remove_recursive(*root_, levels, 0, client_id);
}

void SubscriptionTrie::remove_all_recursive(Node &node,
                                            std::string_view client_id) {
  node.subscriptions.erase(std::string{client_id});

  std::vector<std::string> to_prune;
  for (auto &[key, child] : node.children) {
    remove_all_recursive(*child, client_id);
    if (child->empty()) {
      to_prune.push_back(key);
    }
  }
  for (const auto &key : to_prune) {
    node.children.erase(key);
  }
}

void SubscriptionTrie::remove_all(std::string_view client_id) {
  remove_all_recursive(*root_, client_id);
}

std::size_t SubscriptionTrie::count_recursive(const Node &node) noexcept {
  std::size_t total = node.subscriptions.size();
  for (const auto &[key, child] : node.children) {
    total += count_recursive(*child);
  }
  return total;
}

std::size_t SubscriptionTrie::size() const noexcept {
  return count_recursive(*root_);
}

} // namespace mqtt
