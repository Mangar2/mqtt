#include "authz/acl_loader.h"

#include "authz/authz_error.h"

namespace mqtt {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AclLoader::AclLoader(AclEngine &engine) : engine_(engine) {}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void AclLoader::load(const std::vector<AclRuleConfig> &config) {
  engine_.reload(parse(config));
}

void AclLoader::reload(const std::vector<AclRuleConfig> &config) {
  engine_.reload(parse(config));
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::vector<AclRule>
AclLoader::parse(const std::vector<AclRuleConfig> &config) {
  std::vector<AclRule> rules;
  rules.reserve(config.size());
  for (const AclRuleConfig &cfg : config) {
    rules.push_back(parse_rule(cfg));
  }
  return rules;
}

AclRule AclLoader::parse_rule(const AclRuleConfig &cfg) {
  AclAction action{};
  if (cfg.action == "publish") {
    action = AclAction::Publish;
  } else if (cfg.action == "subscribe") {
    action = AclAction::Subscribe;
  } else if (cfg.action == "publish_and_subscribe") {
    action = AclAction::PublishAndSubscribe;
  } else {
    throw AuthzException(AuthzError::InvalidAction,
                         "Unknown ACL action: " + cfg.action);
  }

  AclEffect effect{};
  if (cfg.effect == "allow") {
    effect = AclEffect::Allow;
  } else if (cfg.effect == "deny") {
    effect = AclEffect::Deny;
  } else {
    throw AuthzException(AuthzError::InvalidEffect,
                         "Unknown ACL effect: " + cfg.effect);
  }

  return AclRule{.principal = cfg.principal,
                 .topic_pattern = cfg.topic_pattern,
                 .action = action,
                 .effect = effect};
}

} // namespace mqtt
