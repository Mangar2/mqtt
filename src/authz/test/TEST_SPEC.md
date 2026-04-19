# TEST_SPEC — authz (Module 9)

Unit tests for `AclEngine` and `AclLoader`.

---

## AclEngine — basic allow/deny

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `empty_engine_denies_publish` | No rules → default deny | publish `"a/b"` | `false` |
| `empty_engine_denies_subscribe` | No rules → default deny | subscribe `"a/b"` | `false` |
| `exact_publish_allow` | Single allow rule, exact topic, publish | rule: `* a/b pub allow`; check publish `"a/b"` | `true` |
| `exact_publish_deny` | Single deny rule, exact topic, publish | rule: `* a/b pub deny`; check publish `"a/b"` | `false` |
| `exact_subscribe_allow` | Single allow rule, exact topic, subscribe | rule: `* a/b sub allow`; check subscribe `"a/b"` | `true` |
| `exact_subscribe_deny` | Deny rule stops subscribe | rule: `* a/b sub deny`; check subscribe `"a/b"` | `false` |

---

## AclEngine — action specificity

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `publish_rule_does_not_cover_subscribe` | Publish-only rule; subscribe checked | rule: `* a/b publish allow`; check subscribe | `false` |
| `subscribe_rule_does_not_cover_publish` | Subscribe-only rule; publish checked | rule: `* a/b subscribe allow`; check publish | `false` |
| `combined_action_covers_both` | PublishAndSubscribe rule | rule: `* a/b pub_and_sub allow`; check both | both `true` |

---

## AclEngine — first-match-wins

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `first_allow_wins_over_later_deny` | Allow rule before deny rule | rules: allow then deny, same principal+topic | `true` |
| `first_deny_wins_over_later_allow` | Deny rule before allow rule | rules: deny then allow, same principal+topic | `false` |

---

## AclEngine — principal matching

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `wildcard_principal_matches_any_client` | `"*"` principal | rule principal `"*"`; any client_id | `true` |
| `principal_anonymous_matches_empty_username` | Anonymous pseudo-principal | rule principal `"anonymous"`; empty username | `true` |
| `principal_matches_client_id` | Exact client_id match | rule principal `"dev1"`; client_id `"dev1"` | `true` |
| `principal_matches_username` | Exact username match | rule principal `"alice"`; username `"alice"` | `true` |
| `principal_no_match` | Wrong principal | rule principal `"dev1"`; client_id `"dev2"` | `false` |

---

## AclEngine — wildcard topic matching (9.1.4)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `plus_wildcard_single_level` | `+` matches one level | pattern `"a/+/c"`; topic `"a/b/c"` | `true` |
| `plus_wildcard_does_not_match_multi_level` | `+` does not cross `/` | pattern `"a/+"`; topic `"a/b/c"` | `false` |
| `hash_wildcard_matches_remaining_levels` | `#` matches remaining | pattern `"a/#"`; topic `"a/b/c/d"` | `true` |
| `hash_wildcard_matches_zero_levels` | `#` after `/` matches direct child | pattern `"a/#"`; topic `"a"` | `false` (no trailing `/`) |
| `hash_wildcard_exact_prefix` | `#` with no prefix | pattern `"#"`; topic `"x/y/z"` | `true` |
| `hash_wildcard_matches_single_level` | `#` matches one level | pattern `"a/#"`; topic `"a/b"` | `true` |
| `no_wildcard_no_match` | Exact pattern; wrong topic | pattern `"a/b"`; topic `"a/c"` | `false` |
| `subscribe_filter_as_literal` | Subscribe filter checked as literal string | pattern `"sensor/#"`; filter `"sensor/+"` | `true` (# covers all remaining) |

---

## AclEngine — reload

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `reload_replaces_rules` | Old rules gone after reload | load allow; reload with deny; check | `false` |
| `reload_with_empty_clears_rules` | Reload empty → deny all | load allow; reload `{}`; check publish | `false` |

---

## AclLoader — parsing

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `loader_load_populates_engine` | Load config and verify engine works | config allow rule; check publish | `true` |
| `loader_reload_replaces_engine_rules` | Reload changes behaviour | load allow; reload deny; check | `false` |
| `loader_invalid_action_throws` | Bad action string | config with action `"write"` | throws `AuthzException(AuthzError::InvalidAction)` |
| `loader_invalid_effect_throws` | Bad effect string | config with effect `"maybe"` | throws `AuthzException(AuthzError::InvalidEffect)` |
| `loader_all_action_strings` | All three action strings parse | `"publish"`, `"subscribe"`, `"publish_and_subscribe"` | no throw; correct `AclAction` |
| `loader_all_effect_strings` | Both effect strings parse | `"allow"`, `"deny"` | no throw; correct `AclEffect` |

---

## Broker ACL startup policy helper

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `broker_acl_policy_includes_internal_rule_and_configured_rules` | Startup policy prepends internal broker principal and preserves configured order | two configured rules, allow_anonymous=false | first rule is internal allow-all, followed by configured rules unchanged |
| `broker_acl_policy_appends_anonymous_fallback_when_enabled` | Anonymous fallback is added only when enabled | configured rules + allow_anonymous=true | result contains trailing `*` publish_and_subscribe allow rule |
| `broker_acl_policy_omits_anonymous_fallback_when_disabled` | No anonymous fallback in strict mode | configured rules + allow_anonymous=false | no `*` fallback rule present |
