#include "authz/acl_engine.h"

#include <mutex>

namespace mqtt {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AclEngine::AclEngine(std::vector<AclRule> rules) : rules_(std::move(rules)) {}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool AclEngine::check_publish(std::string_view client_id,
                              std::string_view username,
                              std::string_view topic) const noexcept {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return evaluate(client_id, username, topic, AclAction::Publish) ==
         AclEffect::Allow;
}

bool AclEngine::check_subscribe(std::string_view client_id,
                                std::string_view username,
                                std::string_view topic_filter) const noexcept {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return evaluate(client_id, username, topic_filter, AclAction::Subscribe) ==
         AclEffect::Allow;
}

void AclEngine::reload(std::vector<AclRule> rules) noexcept {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  rules_ = std::move(rules);
}

const std::vector<AclRule> &AclEngine::rules() const noexcept {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return rules_;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool AclEngine::matches_principal(const AclRule &rule,
                                  std::string_view client_id,
                                  std::string_view username) noexcept {
  if (rule.principal == "*") {
    return true;
  }
  if (rule.principal == "anonymous" && username.empty()) {
    return true;
  }
  return rule.principal == client_id || rule.principal == username;
}

bool AclEngine::matches_topic_pattern(std::string_view pattern,
                                      std::string_view topic) noexcept {
  // Iterative level-by-level matching.
  // pos_p: current position in pattern; pos_t: current position in topic.
  std::size_t pos_p = 0U;
  std::size_t pos_t = 0U;

  while (pos_p < pattern.size() && pos_t <= topic.size()) {
    // Locate the end of the current pattern level.
    std::size_t end_p = pattern.find('/', pos_p);
    if (end_p == std::string_view::npos) {
      end_p = pattern.size();
    }
    std::string_view pat_level = pattern.substr(pos_p, end_p - pos_p);

    if (pat_level == "#") {
      // Multi-level wildcard: matches everything remaining.
      return true;
    }

    // Locate the end of the current topic level.
    std::size_t end_t = topic.find('/', pos_t);
    if (end_t == std::string_view::npos) {
      end_t = topic.size();
    }
    std::string_view top_level = topic.substr(pos_t, end_t - pos_t);

    if (pat_level != "+" && pat_level != top_level) {
      // Exact mismatch and not a single-level wildcard.
      return false;
    }

    // Advance past the consumed level and separator.
    pos_p = (end_p < pattern.size()) ? end_p + 1U : end_p;
    pos_t = (end_t < topic.size()) ? end_t + 1U : end_t;
  }

  // Both must be exhausted simultaneously.
  return pos_p == pattern.size() && pos_t == topic.size();
}

bool AclEngine::action_covers(AclAction rule_action,
                              AclAction requested) noexcept {
  return rule_action == AclAction::PublishAndSubscribe ||
         rule_action == requested;
}

AclEffect AclEngine::evaluate(std::string_view client_id,
                              std::string_view username, std::string_view topic,
                              AclAction action) const noexcept {
  for (const AclRule &rule : rules_) {
    if (!matches_principal(rule, client_id, username)) {
      continue;
    }
    if (!action_covers(rule.action, action)) {
      continue;
    }
    if (!matches_topic_pattern(rule.topic_pattern, topic)) {
      continue;
    }
    return rule.effect;
  }
  return AclEffect::Deny;
}

} // namespace mqtt
