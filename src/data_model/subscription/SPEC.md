# subscription — Module 1.6

Pure data structures for MQTT 5.0 subscriptions. No logic, no I/O, no external dependencies.
All types live in the `mqtt` namespace.

## Files

| File | Plan ref | Contents |
|------|----------|----------|
| `retain_handling.h`      | 1.6.2 | `RetainHandling` enum |
| `subscription_options.h` | 1.6.2 | `SubscriptionOptions` struct |
| `subscription.h`         | 1.6.1 | `Subscription` struct |
| `shared_subscription.h`  | 1.6.3 | `SharedSubscription` struct |

## Types

### RetainHandling (1.6.2)

Controls when retained messages are forwarded to a new subscriber.
Encoded in bits [4:5] of the Subscription Options byte (MQTT 5.0 Section 3.8.3.1).

| Value | Name | Meaning |
|-------|------|---------|
| 0 | `SendAtSubscribe` | Send retained messages at subscribe time. |
| 1 | `SendIfNew` | Send retained messages only if this is a new subscription. |
| 2 | `Never` | Never send retained messages on subscribe. |

### SubscriptionOptions (1.6.2)

Aggregates the three option flags from the Subscription Options byte:

- `no_local` (bool, default `false`): Suppress delivery of messages published by this client's own connection.
- `retain_as_published` (bool, default `false`): Forward the RETAIN flag as received from the publisher.
- `retain_handling` (RetainHandling, default `SendAtSubscribe`): Controls retained message delivery on subscribe.

Supports `operator==` via `= default`.

### Subscription (1.6.1)

Represents a single subscription held by a session:

- `topic_filter` (Utf8String): Topic filter; may contain `+` or `#` wildcards.
- `qos` (QoS, default `AtMostOnce`): Maximum delivery QoS for messages on this subscription.
- `options` (SubscriptionOptions): Delivery options.
- `identifier` (std::optional\<uint32_t\>): Subscription Identifier; valid range [1, 268 435 455].
  Absent (`nullopt`) if the client did not include a Subscription Identifier.

Supports `operator==` via `= default`.

### SharedSubscription (1.6.3)

Models a shared subscription of the form `$share/<group>/<topic_filter>`:

- `group` (Utf8String): Share group name; must not be empty.
- `topic_filter` (Utf8String): The underlying topic filter; may contain wildcards.

Shared subscriptions allow multiple clients in the same group to share message delivery.
Round-robin dispatch is implemented in Module 12.5.

Supports `operator==` via `= default`.

## Design rules

- Header-only; one struct/enum per file.
