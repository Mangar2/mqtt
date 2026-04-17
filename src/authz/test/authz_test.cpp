#include <catch2/catch_test_macros.hpp>

#include "authz/acl_engine.h"
#include "authz/acl_loader.h"
#include "authz/acl_rule.h"
#include "authz/authz_error.h"

using namespace mqtt;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

AclRule make_rule(std::string principal, std::string topic_pattern,
                  AclAction action, AclEffect effect) {
  return AclRule{.principal = std::move(principal),
                 .topic_pattern = std::move(topic_pattern),
                 .action = action,
                 .effect = effect};
}

} // namespace

// ---------------------------------------------------------------------------
// AclEngine — basic allow / deny
// ---------------------------------------------------------------------------

TEST_CASE("empty_engine_denies_publish", "[authz]") {
  AclEngine engine;
  CHECK_FALSE(engine.check_publish("cli", "user", "a/b"));
}

TEST_CASE("empty_engine_denies_subscribe", "[authz]") {
  AclEngine engine;
  CHECK_FALSE(engine.check_subscribe("cli", "user", "a/b"));
}

TEST_CASE("exact_publish_allow", "[authz]") {
  AclEngine engine(
      {make_rule("*", "a/b", AclAction::Publish, AclEffect::Allow)});
  CHECK(engine.check_publish("cli", "user", "a/b"));
}

TEST_CASE("exact_publish_deny", "[authz]") {
  AclEngine engine(
      {make_rule("*", "a/b", AclAction::Publish, AclEffect::Deny)});
  CHECK_FALSE(engine.check_publish("cli", "user", "a/b"));
}

TEST_CASE("exact_subscribe_allow", "[authz]") {
  AclEngine engine(
      {make_rule("*", "a/b", AclAction::Subscribe, AclEffect::Allow)});
  CHECK(engine.check_subscribe("cli", "user", "a/b"));
}

TEST_CASE("exact_subscribe_deny", "[authz]") {
  AclEngine engine(
      {make_rule("*", "a/b", AclAction::Subscribe, AclEffect::Deny)});
  CHECK_FALSE(engine.check_subscribe("cli", "user", "a/b"));
}

// ---------------------------------------------------------------------------
// AclEngine — action specificity
// ---------------------------------------------------------------------------

TEST_CASE("publish_rule_does_not_cover_subscribe", "[authz]") {
  AclEngine engine(
      {make_rule("*", "a/b", AclAction::Publish, AclEffect::Allow)});
  CHECK_FALSE(engine.check_subscribe("cli", "user", "a/b"));
}

TEST_CASE("subscribe_rule_does_not_cover_publish", "[authz]") {
  AclEngine engine(
      {make_rule("*", "a/b", AclAction::Subscribe, AclEffect::Allow)});
  CHECK_FALSE(engine.check_publish("cli", "user", "a/b"));
}

TEST_CASE("combined_action_covers_both", "[authz]") {
  AclEngine engine({make_rule("*", "a/b", AclAction::PublishAndSubscribe,
                              AclEffect::Allow)});
  CHECK(engine.check_publish("cli", "user", "a/b"));
  CHECK(engine.check_subscribe("cli", "user", "a/b"));
}

// ---------------------------------------------------------------------------
// AclEngine — first-match-wins
// ---------------------------------------------------------------------------

TEST_CASE("first_allow_wins_over_later_deny", "[authz]") {
  AclEngine engine({
      make_rule("*", "a/b", AclAction::Publish, AclEffect::Allow),
      make_rule("*", "a/b", AclAction::Publish, AclEffect::Deny),
  });
  CHECK(engine.check_publish("cli", "user", "a/b"));
}

TEST_CASE("first_deny_wins_over_later_allow", "[authz]") {
  AclEngine engine({
      make_rule("*", "a/b", AclAction::Publish, AclEffect::Deny),
      make_rule("*", "a/b", AclAction::Publish, AclEffect::Allow),
  });
  CHECK_FALSE(engine.check_publish("cli", "user", "a/b"));
}

// ---------------------------------------------------------------------------
// AclEngine — principal matching
// ---------------------------------------------------------------------------

TEST_CASE("wildcard_principal_matches_any_client", "[authz]") {
  AclEngine engine(
      {make_rule("*", "a/b", AclAction::Publish, AclEffect::Allow)});
  CHECK(engine.check_publish("anyone", "", "a/b"));
  CHECK(engine.check_publish("other", "whoever", "a/b"));
}

TEST_CASE("principal_matches_client_id", "[authz]") {
  AclEngine engine(
      {make_rule("dev1", "a/b", AclAction::Publish, AclEffect::Allow)});
  CHECK(engine.check_publish("dev1", "", "a/b"));
}

TEST_CASE("principal_matches_username", "[authz]") {
  AclEngine engine(
      {make_rule("alice", "a/b", AclAction::Publish, AclEffect::Allow)});
  CHECK(engine.check_publish("cli", "alice", "a/b"));
}

TEST_CASE("principal_no_match", "[authz]") {
  AclEngine engine(
      {make_rule("dev1", "a/b", AclAction::Publish, AclEffect::Allow)});
  CHECK_FALSE(engine.check_publish("dev2", "bob", "a/b"));
}

// ---------------------------------------------------------------------------
// AclEngine — wildcard topic matching (9.1.4)
// ---------------------------------------------------------------------------

TEST_CASE("plus_wildcard_single_level", "[authz]") {
  AclEngine engine(
      {make_rule("*", "a/+/c", AclAction::Publish, AclEffect::Allow)});
  CHECK(engine.check_publish("cli", "", "a/b/c"));
}

TEST_CASE("plus_wildcard_does_not_match_multi_level", "[authz]") {
  AclEngine engine(
      {make_rule("*", "a/+", AclAction::Publish, AclEffect::Allow)});
  CHECK_FALSE(engine.check_publish("cli", "", "a/b/c"));
}

TEST_CASE("hash_wildcard_matches_remaining_levels", "[authz]") {
  AclEngine engine(
      {make_rule("*", "a/#", AclAction::Publish, AclEffect::Allow)});
  CHECK(engine.check_publish("cli", "", "a/b/c/d"));
}

TEST_CASE("hash_wildcard_matches_single_level", "[authz]") {
  AclEngine engine(
      {make_rule("*", "a/#", AclAction::Publish, AclEffect::Allow)});
  CHECK(engine.check_publish("cli", "", "a/b"));
}

TEST_CASE("hash_wildcard_exact_prefix", "[authz]") {
  AclEngine engine({make_rule("*", "#", AclAction::Publish, AclEffect::Allow)});
  CHECK(engine.check_publish("cli", "", "x/y/z"));
}

TEST_CASE("no_wildcard_no_match", "[authz]") {
  AclEngine engine(
      {make_rule("*", "a/b", AclAction::Publish, AclEffect::Allow)});
  CHECK_FALSE(engine.check_publish("cli", "", "a/c"));
}

TEST_CASE("subscribe_filter_as_literal", "[authz]") {
  // # in ACL pattern covers subscribe filter "sensor/+" (treated as literal)
  AclEngine engine(
      {make_rule("*", "sensor/#", AclAction::Subscribe, AclEffect::Allow)});
  CHECK(engine.check_subscribe("cli", "", "sensor/+"));
}

// ---------------------------------------------------------------------------
// AclEngine — reload
// ---------------------------------------------------------------------------

TEST_CASE("reload_replaces_rules", "[authz]") {
  AclEngine engine(
      {make_rule("*", "a/b", AclAction::Publish, AclEffect::Allow)});
  REQUIRE(engine.check_publish("cli", "", "a/b"));

  engine.reload({make_rule("*", "a/b", AclAction::Publish, AclEffect::Deny)});
  CHECK_FALSE(engine.check_publish("cli", "", "a/b"));
}

TEST_CASE("reload_with_empty_clears_rules", "[authz]") {
  AclEngine engine(
      {make_rule("*", "a/b", AclAction::Publish, AclEffect::Allow)});
  REQUIRE(engine.check_publish("cli", "", "a/b"));

  engine.reload({});
  CHECK_FALSE(engine.check_publish("cli", "", "a/b"));
}

// ---------------------------------------------------------------------------
// AclLoader — parsing
// ---------------------------------------------------------------------------

TEST_CASE("loader_load_populates_engine", "[authz]") {
  AclEngine engine;
  AclLoader loader(engine);

  loader.load({{.principal = "*",
                .topic_pattern = "sensor/data",
                .action = "publish",
                .effect = "allow"}});

  CHECK(engine.check_publish("cli", "", "sensor/data"));
}

TEST_CASE("loader_reload_replaces_engine_rules", "[authz]") {
  AclEngine engine;
  AclLoader loader(engine);

  loader.load({{.principal = "*",
                .topic_pattern = "sensor/data",
                .action = "publish",
                .effect = "allow"}});
  REQUIRE(engine.check_publish("cli", "", "sensor/data"));

  loader.reload({{.principal = "*",
                  .topic_pattern = "sensor/data",
                  .action = "publish",
                  .effect = "deny"}});
  CHECK_FALSE(engine.check_publish("cli", "", "sensor/data"));
}

TEST_CASE("loader_invalid_action_throws", "[authz]") {
  AclEngine engine;
  AclLoader loader(engine);

  const AclRuleConfig bad_config{.principal = "*",
                                 .topic_pattern = "a/b",
                                 .action = "write",
                                 .effect = "allow"};
  try {
    loader.load({bad_config});
    FAIL("Expected AuthzException");
  } catch (const AuthzException &exc) {
    CHECK(exc.error() == AuthzError::InvalidAction);
  }
}

TEST_CASE("loader_invalid_effect_throws", "[authz]") {
  AclEngine engine;
  AclLoader loader(engine);

  const AclRuleConfig bad_config{.principal = "*",
                                 .topic_pattern = "a/b",
                                 .action = "publish",
                                 .effect = "maybe"};
  try {
    loader.load({bad_config});
    FAIL("Expected AuthzException");
  } catch (const AuthzException &exc) {
    CHECK(exc.error() == AuthzError::InvalidEffect);
  }
}

TEST_CASE("loader_all_action_strings", "[authz]") {
  AclEngine engine;
  AclLoader loader(engine);

  CHECK_NOTHROW(loader.load({
      {.principal = "*",
       .topic_pattern = "a",
       .action = "publish",
       .effect = "allow"},
      {.principal = "*",
       .topic_pattern = "b",
       .action = "subscribe",
       .effect = "allow"},
      {.principal = "*",
       .topic_pattern = "c",
       .action = "publish_and_subscribe",
       .effect = "allow"},
  }));

  const auto &rules = engine.rules();
  REQUIRE(rules.size() == 3U);
  CHECK(rules[0].action == AclAction::Publish);
  CHECK(rules[1].action == AclAction::Subscribe);
  CHECK(rules[2].action == AclAction::PublishAndSubscribe);
}

TEST_CASE("loader_all_effect_strings", "[authz]") {
  AclEngine engine;
  AclLoader loader(engine);

  CHECK_NOTHROW(loader.load({
      {.principal = "*",
       .topic_pattern = "a",
       .action = "publish",
       .effect = "allow"},
      {.principal = "*",
       .topic_pattern = "b",
       .action = "publish",
       .effect = "deny"},
  }));

  const auto &rules = engine.rules();
  REQUIRE(rules.size() == 2U);
  CHECK(rules[0].effect == AclEffect::Allow);
  CHECK(rules[1].effect == AclEffect::Deny);
}
