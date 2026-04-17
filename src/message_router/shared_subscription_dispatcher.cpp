#include "message_router/shared_subscription_dispatcher.h"

#include <algorithm>

namespace mqtt {

// ---------------------------------------------------------------------------
// Key helpers
// ---------------------------------------------------------------------------

std::string
SharedSubscriptionDispatcher::make_key(std::string_view group,
                                       std::string_view topic_filter) {
  std::string key;
  key.reserve(group.size() + 1U + topic_filter.size());
  key.append(group);
  key.push_back('\0');
  key.append(topic_filter);
  return key;
}

// ---------------------------------------------------------------------------
// Topic filter matching
// ---------------------------------------------------------------------------

bool SharedSubscriptionDispatcher::match_recursive(std::string_view topic,
                                                   std::string_view filter,
                                                   bool is_root) {
  // '#' matches zero or more remaining levels — always a match here.
  if (filter == "#") {
    return true;
  }

  // Both exhausted simultaneously — exact match.
  if (topic.empty() && filter.empty()) {
    return true;
  }

  // One is exhausted but the other is not (and filter is not '#').
  if (topic.empty() || filter.empty()) {
    return false;
  }

  // Split first level from each string.
  const auto topic_slash = topic.find('/');
  const auto filter_slash = filter.find('/');

  const std::string_view topic_level = topic.substr(
      0U, topic_slash != std::string_view::npos ? topic_slash : topic.size());
  const std::string_view filter_level =
      filter.substr(0U, filter_slash != std::string_view::npos ? filter_slash
                                                               : filter.size());

  // MQTT §4.7.2: topics beginning with '$' are not matched by first-level
  // '+' or '#' wildcards.
  if (is_root && !topic_level.empty() && topic_level[0] == '$') {
    if (filter_level == "+" || filter_level == "#") {
      return false;
    }
  }

  // Current-level match: '+' covers any single level, otherwise exact.
  if (filter_level != "+" && filter_level != topic_level) {
    return false;
  }

  // Both end here — levels matched.
  if (topic_slash == std::string_view::npos &&
      filter_slash == std::string_view::npos) {
    return true;
  }

  // One side has more levels but the other does not.
  if (topic_slash == std::string_view::npos ||
      filter_slash == std::string_view::npos) {
    return false;
  }

  // Recurse into remaining levels.
  return match_recursive(topic.substr(topic_slash + 1U),
                         filter.substr(filter_slash + 1U), false);
}

bool SharedSubscriptionDispatcher::matches_filter(
    std::string_view topic_name, std::string_view topic_filter) {
  return match_recursive(topic_name, topic_filter, true);
}

// ---------------------------------------------------------------------------
// Member management
// ---------------------------------------------------------------------------

void SharedSubscriptionDispatcher::add_member(std::string_view group,
                                              std::string_view topic_filter,
                                              std::string_view client_id,
                                              const Subscription &sub) {
  auto &state = groups_[make_key(group, topic_filter)];

  // Replace existing entry for this client if present.
  auto member_it = std::find_if(state.members.begin(), state.members.end(),
                                [client_id](const MemberEntry &entry) {
                                  return entry.client_id == client_id;
                                });

  if (member_it != state.members.end()) {
    member_it->subscription = sub;
  } else {
    state.members.push_back(MemberEntry{
        .client_id = std::string(client_id),
        .subscription = sub,
    });
  }
}

void SharedSubscriptionDispatcher::remove_member(std::string_view group,
                                                 std::string_view topic_filter,
                                                 std::string_view client_id) {
  const std::string key = make_key(group, topic_filter);
  auto group_it = groups_.find(key);
  if (group_it == groups_.end()) {
    return;
  }

  auto &state = group_it->second;
  auto member_it = std::find_if(state.members.begin(), state.members.end(),
                                [client_id](const MemberEntry &entry) {
                                  return entry.client_id == client_id;
                                });

  if (member_it == state.members.end()) {
    return;
  }

  // Keep round-robin cursor valid after removal.
  const std::size_t removed_idx =
      static_cast<std::size_t>(member_it - state.members.begin());
  state.members.erase(member_it);

  if (state.members.empty()) {
    groups_.erase(group_it); // 12.5.3 — clean up empty group.
    return;
  }

  if (state.next_idx > removed_idx) {
    --state.next_idx;
  }
  // Wrap around in case next_idx now equals members.size().
  if (state.next_idx >= state.members.size()) {
    state.next_idx = 0U;
  }
}

void SharedSubscriptionDispatcher::remove_client(std::string_view client_id) {
  for (auto iter = groups_.begin(); iter != groups_.end();) {
    auto &state = iter->second;

    auto member_it = std::find_if(state.members.begin(), state.members.end(),
                                  [client_id](const MemberEntry &entry) {
                                    return entry.client_id == client_id;
                                  });

    if (member_it != state.members.end()) {
      const std::size_t removed_idx =
          static_cast<std::size_t>(member_it - state.members.begin());
      state.members.erase(member_it);

      if (state.members.empty()) {
        iter = groups_.erase(iter);
        continue;
      }

      if (state.next_idx > removed_idx) {
        --state.next_idx;
      }
      if (state.next_idx >= state.members.size()) {
        state.next_idx = 0U;
      }
    }

    ++iter;
  }
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

std::vector<MatchResult> SharedSubscriptionDispatcher::select_next_for_topic(
    std::string_view topic_name) {
  std::vector<MatchResult> results;

  for (auto &[key, state] : groups_) {
    if (state.members.empty()) {
      continue;
    }

    // Extract the topic_filter from the composite key (after the '\0').
    const std::string_view key_view(key);
    const auto null_pos = key_view.find('\0');
    if (null_pos == std::string_view::npos) {
      continue;
    }
    const std::string_view stored_filter = key_view.substr(null_pos + 1U);

    if (!matches_filter(topic_name, stored_filter)) {
      continue;
    }

    // Round-robin: pick next member (12.5.2).
    const std::size_t idx = state.next_idx % state.members.size();
    state.next_idx = (idx + 1U) % state.members.size();

    const MemberEntry &chosen = state.members[idx];
    results.push_back(MatchResult{
        .client_id = chosen.client_id,
        .subscription = chosen.subscription,
    });
  }

  return results;
}

std::size_t SharedSubscriptionDispatcher::member_count(
    std::string_view group, std::string_view topic_filter) const noexcept {
  auto iter = groups_.find(make_key(group, topic_filter));
  if (iter == groups_.end()) {
    return 0U;
  }
  return iter->second.members.size();
}

} // namespace mqtt
