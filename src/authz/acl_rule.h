#pragma once

/**
 * @file acl_rule.h
 * @brief AclAction, AclEffect, and AclRule aggregate (Module 9.1.1).
 */

#include <cstdint>
#include <string>

namespace mqtt {

/**
 * @brief The MQTT operation type that an ACL rule controls.
 */
enum class AclAction : uint8_t {
  Publish,             ///< Controls publish operations only.
  Subscribe,           ///< Controls subscribe operations only.
  PublishAndSubscribe, ///< Controls both publish and subscribe operations.
};

/**
 * @brief Whether a matching ACL rule grants or revokes access.
 */
enum class AclEffect : uint8_t {
  Allow, ///< Permit the operation when this rule matches.
  Deny,  ///< Reject the operation when this rule matches.
};

/**
 * @brief A single ACL rule entry (Module 9.1.1).
 *
 * Rules are evaluated in declaration order; the first rule whose `principal`,
 * `topic_pattern`, and `action` all match the request determines the outcome.
 * If no rule matches, the default decision is deny.
 *
 * ### Principal
 * - `"*"` matches every client regardless of identity.
 * - Any other value is compared by exact equality against both the connecting
 *   client's `client_id` and its `username`; a match on either is sufficient.
 *
 * ### Topic pattern
 * The field uses the same wildcard syntax as MQTT topic filters:
 * - `+` matches exactly one topic level.
 * - `#` matches zero or more remaining levels and must appear last.
 * - All other characters are matched literally.
 *
 * When checking a subscribe operation the requested subscription filter is
 * treated as a plain string (its own `+`/`#` characters are literals).
 */
struct AclRule {
  std::string principal;     ///< Principal pattern: `"*"` or exact client_id / username.
  std::string topic_pattern; ///< MQTT-style topic filter; wildcards `+` and `#` allowed.
  AclAction action;          ///< Operation type this rule governs.
  AclEffect effect;          ///< Grant or revoke access.
};

} // namespace mqtt
