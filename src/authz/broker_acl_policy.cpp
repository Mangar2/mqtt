/**
 * @file broker_acl_policy.cpp
 * @brief Broker startup ACL policy helpers.
 */

#include "authz/broker_acl_policy.h"

namespace mqtt {

std::vector<AclRuleConfig>
make_startup_acl_rules(const std::vector<AclRuleConfig> &configured_rules,
                       bool allow_anonymous) {
  std::vector<AclRuleConfig> acl_rules;
  acl_rules.reserve(configured_rules.size() + 2U);

  acl_rules.push_back({.principal = std::string(k_broker_internal_principal),
                       .topic_pattern = "#",
                       .action = "publish_and_subscribe",
                       .effect = "allow"});

  for (const AclRuleConfig &configured_rule : configured_rules) {
    acl_rules.push_back(configured_rule);
  }

  if (allow_anonymous) {
    acl_rules.push_back({.principal = "*",
                         .topic_pattern = "#",
                         .action = "publish_and_subscribe",
                         .effect = "allow"});
  }

  return acl_rules;
}

} // namespace mqtt
