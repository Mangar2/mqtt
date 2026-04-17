#include "topic/topic_matcher.h"

#include <string>
#include <string_view>
#include <vector>

namespace mqtt {

namespace {

/**
 * Splits @p topic on '/' and returns each level segment as a string_view.
 * The views remain valid as long as @p topic is alive.
 */
std::vector<std::string_view> split_topic(std::string_view topic) {
  std::vector<std::string_view> levels;
  std::size_t start = 0U;
  while (start <= topic.size()) {
    const auto pos = topic.find('/', start);
    if (pos == std::string_view::npos) {
      levels.push_back(topic.substr(start));
      break;
    }
    levels.push_back(topic.substr(start, pos - start));
    start = pos + 1U;
  }
  return levels;
}

} // namespace

// ── TopicMatcher
// ──────────────────────────────────────────────────────────────

void TopicMatcher::collect_matches(const SubscriptionTrie::Node &node,
                                   const std::vector<std::string_view> &levels,
                                   std::size_t depth, bool is_system,
                                   std::vector<MatchResult> &results) {
  // Wildcard matching is suppressed at the first level (depth == 0) when the
  // publish topic is a system topic (MQTT 5.0 Section 4.7.2).
  const bool allow_wildcards = !(depth == 0U && is_system);

  // '#' child: matches all remaining levels, including zero.
  if (allow_wildcards) {
    const auto hash_it = node.children.find("#");
    if (hash_it != node.children.end()) {
      for (const auto &[cid, sub] : hash_it->second->subscriptions) {
        results.push_back({cid, sub});
      }
    }
  }

  if (depth < levels.size()) {
    const std::string cur_level{levels[depth]};

    // Exact level match.
    const auto exact_it = node.children.find(cur_level);
    if (exact_it != node.children.end()) {
      collect_matches(*exact_it->second, levels, depth + 1U, is_system,
                      results);
    }

    // '+' wildcard: matches exactly one topic level.
    if (allow_wildcards) {
      const auto plus_it = node.children.find("+");
      if (plus_it != node.children.end()) {
        collect_matches(*plus_it->second, levels, depth + 1U, is_system,
                        results);
      }
    }
  } else {
    // All topic levels consumed: collect subscriptions at the current node.
    for (const auto &[cid, sub] : node.subscriptions) {
      results.push_back({cid, sub});
    }
  }
}

std::vector<MatchResult> TopicMatcher::match(const SubscriptionTrie &trie,
                                             std::string_view topic_name) {
  const auto levels = split_topic(topic_name);
  const bool is_system = !topic_name.empty() && topic_name.front() == '$';

  std::vector<MatchResult> results;
  collect_matches(trie.root(), levels, 0U, is_system, results);
  return results;
}

} // namespace mqtt
