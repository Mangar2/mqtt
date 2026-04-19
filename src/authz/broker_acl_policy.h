#pragma once

/**
 * @file broker_acl_policy.h
 * @brief Broker startup ACL policy helpers.
 */

#include <string_view>
#include <vector>

#include "authz/acl_loader.h"

namespace mqtt {

/**
 * @brief Reserved principal used for broker-internal publishes.
 */
constexpr std::string_view k_broker_internal_principal =
    "_broker_will_system_";

/**
 * @brief Build startup ACL rules from configured rules and broker policy.
 *
 * Base policy:
 * - Always allow the broker internal principal to publish and subscribe.
 * - Append configured rules in configured order.
 * - Optionally append anonymous allow-all fallback.
 *
 * @param configured_rules Rules from configuration.
 * @param allow_anonymous Whether anonymous fallback allow-all is enabled.
 * @return Ordered ACL rule configuration list for startup loading.
 */
[[nodiscard]] std::vector<AclRuleConfig>
make_startup_acl_rules(const std::vector<AclRuleConfig> &configured_rules,
                       bool allow_anonymous);

} // namespace mqtt
