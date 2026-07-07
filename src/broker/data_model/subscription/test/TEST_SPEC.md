# TEST_SPEC — subscription (Module 1.6)

## RetainHandling (1.6.2)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `retain_handling_values` | Enum values match MQTT 5.0 wire encoding | `SendAtSubscribe`, `SendIfNew`, `Never` | underlying uint8_t values 0, 1, 2 |

## SubscriptionOptions (1.6.2)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `subscription_options_defaults` | Default-constructed | none | `no_local == false, retain_as_published == false, retain_handling == SendAtSubscribe` |
| `subscription_options_set_fields` | All fields populated | `no_local = true, retain_as_published = true, retain_handling = Never` | all fields read back correctly |
| `subscription_options_equality` | operator== on identical structs | two default-constructed | equal |
| `subscription_options_inequality` | operator== on differing structs | differ by `no_local` | not equal |

## Subscription (1.6.1)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `subscription_defaults` | Default-constructed | none | `topic_filter empty, qos == AtMostOnce, default options, identifier == nullopt` |
| `subscription_set_fields` | All fields populated | `topic_filter = "sensors/+"`, `qos = AtLeastOnce`, `no_local = true`, `identifier = 42` | all fields read back correctly |
| `subscription_equality` | operator== on identical structs | two default-constructed | equal |
| `subscription_inequality` | operator== on differing structs | differ by `qos` | not equal |
| `subscription_with_identifier` | Identifier present | `identifier = 42` | `has_value() == true`, value == 42 |
| `subscription_without_identifier` | No identifier | default-constructed | `has_value() == false` |

## SharedSubscription (1.6.3)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `shared_subscription_defaults` | Default-constructed | none | `group empty, topic_filter empty` |
| `shared_subscription_set_fields` | Both fields populated | `group = "g1"`, `topic_filter = "sensors/+"` | fields read back correctly |
| `shared_subscription_equality` | operator== on identical structs | two default-constructed | equal |
| `shared_subscription_inequality` | operator== on differing structs | differ by `group` | not equal |
