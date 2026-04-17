# authz — Authorization Module (Module 9)

Checks publish and subscribe permissions for every connected client.
Depends on: `data_model/` (Module 1), `auth/` (Module 8).

---

## Purpose

The Authorization Module enforces an Access Control List (ACL) that governs
which clients may publish to or subscribe on specific topics.  Every rule is
evaluated in declaration order; the first matching rule wins.  If no rule
matches, the default decision is **deny**.

---

## Sub-modules

| File | Plan ref | Contents |
|------|----------|----------|
| `authz_error.h`    | 9     | `AuthzError` enum and `AuthzException` |
| `acl_rule.h`       | 9.1.1 | `AclAction`, `AclEffect`, `AclRule` aggregate |
| `acl_engine.h/.cpp`| 9.1   | `AclEngine` — rule evaluation with wildcard support |
| `acl_loader.h/.cpp`| 9.2   | `AclLoader` — parse config strings into rules and reload at runtime |

---

## AclRule (9.1.1)

```
struct AclRule {
    std::string principal;      // "*" = any; otherwise exact match vs client_id or username
    std::string topic_pattern;  // MQTT topic filter: may contain + and #
    AclAction   action;         // Publish | Subscribe | PublishAndSubscribe
    AclEffect   effect;         // Allow | Deny
};
```

---

## AclEngine (9.1)

### Construction

```cpp
AclEngine engine;                        // empty — all requests denied
AclEngine engine(std::vector<AclRule>);  // initialise with a rule set
```

### API

| Method | Description |
|--------|-------------|
| `check_publish(client_id, username, topic)` | Returns `true` when the first matching rule has effect `Allow`. |
| `check_subscribe(client_id, username, filter)` | Same, for subscribe actions. |
| `reload(rules)` | Atomically replaces the active rule set (9.2.2). |
| `rules()` | Read-only view of the current rule set. |

### Principal matching (9.1.1)

- `principal == "*"` matches every client.
- Otherwise the principal is compared by exact equality against both
  `client_id` and `username`; a match on either is sufficient.

### Topic wildcard matching (9.1.4)

The ACL rule's `topic_pattern` may contain MQTT wildcard characters.
The topic or subscription filter being checked is treated as a sequence of
slash-delimited levels where only the **pattern's** `+` and `#` are wildcards:

- `+` in the pattern matches exactly one level in the checked string.
- `#` in the pattern matches zero or more remaining levels.
- All other characters are compared literally.

Consequence: a subscription filter such as `sensor/+` is matched as if it were
a topic name — the `+` in the filter is a literal character, while `+` in the
ACL pattern is the wildcard.

### Action matching

| Rule action           | Covers request type |
|-----------------------|---------------------|
| `Publish`             | Publish only        |
| `Subscribe`           | Subscribe only      |
| `PublishAndSubscribe` | Both                |

### Default decision

If no rule matches, `check_publish` and `check_subscribe` both return `false`
(deny).

---

## AclLoader (9.2)

Parses a vector of `AclRuleConfig` records (plain-string fields) into
`AclRule` values and pushes them into an `AclEngine`.

### AclRuleConfig fields

| Field           | Accepted string values |
|-----------------|------------------------|
| `principal`     | `"*"` or any non-empty string |
| `topic_pattern` | Any string (validated at runtime) |
| `action`        | `"publish"` / `"subscribe"` / `"publish_and_subscribe"` |
| `effect`        | `"allow"` / `"deny"` |

### Error handling

Throws `AuthzException(AuthzError::InvalidAction)` for unknown action strings.
Throws `AuthzException(AuthzError::InvalidEffect)` for unknown effect strings.

### load vs reload

- `load(config)` — initial population; called once at start-up.
- `reload(config)` — hot reload; replaces the rule set while the server is live.

Both call `AclEngine::reload` internally; the two entry points are provided
to make intent explicit in call sites.

---

## Constraints

- No thread-safety guarantee — external synchronisation is required when
  `reload` and `check_*` may be called concurrently.
- Rule order is preserved; earlier rules take precedence.
- An empty rule set denies all requests.
