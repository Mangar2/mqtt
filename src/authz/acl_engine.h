#pragma once

/**
 * @file acl_engine.h
 * @brief AclEngine — ACL rule evaluation with wildcard topic matching
 * (Module 9.1).
 */

#include "authz/acl_rule.h"
#include <shared_mutex>
#include <string_view>
#include <vector>

namespace mqtt {

/**
 * @brief Evaluates a prioritised list of ACL rules to decide whether a
 * client is permitted to publish or subscribe (Module 9.1).
 *
 * Rules are stored in declaration order.  The first rule whose principal,
 * topic pattern, and action all match the request determines the decision.
 * When no rule matches the default decision is **deny** (`false`).
 *
 * Wildcard matching in topic patterns follows MQTT Section 4.7 semantics:
 * `+` covers exactly one slash-delimited level; `#` covers zero or more
 * trailing levels.  In the topic or filter string being checked, these
 * characters are treated as literals (9.1.4).
 *
 * Thread safety: none — external synchronisation required when `reload` and
 * `check_*` may execute concurrently.
 */
class AclEngine {
public:
  /** @brief Construct an engine with an empty rule set (all requests denied).
   */
  AclEngine() = default;

  /**
   * @brief Construct an engine and populate it with an initial rule set.
   * @param rules Ordered list of ACL rules; earlier entries take precedence.
   */
  explicit AclEngine(std::vector<AclRule> rules);

  /**
   * @brief Decide whether the client may publish to the given topic (9.1.2).
   *
   * @param client_id Client identifier from the CONNECT packet.
   * @param username  Optional username; pass an empty string when absent.
   * @param topic     Publish topic name (no wildcards).
   * @return `true` when the first matching rule has effect `Allow`;
   *         `false` when the first matching rule has effect `Deny` or no rule
   *         matches (default deny).
   */
  [[nodiscard]] bool check_publish(std::string_view client_id,
                                   std::string_view username,
                                   std::string_view topic) const noexcept;

  /**
   * @brief Decide whether the client may subscribe using the given filter
   * (9.1.3).
   *
   * The filter string is matched against rule topic patterns as a literal
   * sequence of slash-delimited levels; wildcard characters in the filter are
   * not treated as wildcards during ACL evaluation (9.1.4).
   *
   * @param client_id     Client identifier from the CONNECT packet.
   * @param username      Optional username; pass an empty string when absent.
   * @param topic_filter  Requested subscription filter.
   * @return `true` when the first matching rule has effect `Allow`;
   *         `false` otherwise.
   */
  [[nodiscard]] bool
  check_subscribe(std::string_view client_id, std::string_view username,
                  std::string_view topic_filter) const noexcept;

  /**
   * @brief Replace the active rule set (9.2.2 runtime reload).
   *
   * All subsequent `check_*` calls use the new rules exclusively.
   *
   * @param rules New ordered rule set.
   */
  void reload(std::vector<AclRule> rules) noexcept;

  /**
   * @brief Read-only access to the current rule set.
   * @return Const reference to the internal rule vector.
   */
  [[nodiscard]] const std::vector<AclRule> &rules() const noexcept;

private:
  mutable std::shared_mutex mutex_;
  std::vector<AclRule> rules_; ///< Active ACL rules in evaluation order.

  /**
   * @brief Check whether a rule's principal matches the requesting client.
   *
   * @param rule      The rule under evaluation.
   * @param client_id Connecting client's identifier.
   * @param username  Connecting client's username (may be empty).
   * @return `true` when the principal is `"*"` or equals either identity.
   */
  [[nodiscard]] static bool
  matches_principal(const AclRule &rule, std::string_view client_id,
                    std::string_view username) noexcept;

  /**
   * @brief Match a topic or subscription filter against an ACL topic pattern.
   *
   * Pattern wildcards (`+`, `#`) are interpreted according to MQTT Section 4.7.
   * Characters in `topic` are always treated as literals.
   *
   * @param pattern ACL rule's topic pattern (may contain `+` and `#`).
   * @param topic   Topic name or subscription filter string to test.
   * @return `true` when the pattern matches the full topic string.
   */
  [[nodiscard]] static bool
  matches_topic_pattern(std::string_view pattern,
                        std::string_view topic) noexcept;

  /**
   * @brief Return whether the rule's action covers the requested operation.
   *
   * @param rule_action The action stored in the rule.
   * @param requested   The action the client is attempting.
   * @return `true` when `rule_action` is `PublishAndSubscribe`, or equals
   *         `requested` exactly.
   */
  [[nodiscard]] static bool action_covers(AclAction rule_action,
                                          AclAction requested) noexcept;

  /**
   * @brief Core evaluation: find the first matching rule and return its effect.
   *
   * @param client_id Client identifier.
   * @param username  Username (may be empty).
   * @param topic     Topic or filter string.
   * @param action    Operation being requested.
   * @return `AclEffect::Allow` when a matching allow rule is found;
   *         `AclEffect::Deny` when a matching deny rule is found or no rule
   *         matches.
   */
  [[nodiscard]] AclEffect evaluate(std::string_view client_id,
                                   std::string_view username,
                                   std::string_view topic,
                                   AclAction action) const noexcept;
};

} // namespace mqtt
