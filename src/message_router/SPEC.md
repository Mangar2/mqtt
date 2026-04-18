# message_router — Message Router (Module 12)

Dispatches incoming PUBLISH messages to all matching subscribers.
Depends on: `authz/` (9), `connection/` (7.3), `data_model/` (1),
`store/` (4), `topic/` (3), `will_manager/` (11 — via callback).

## Responsibilities

- Resolve Topic Aliases on inbound PUBLISH packets.
- Authorise publish operations against the ACL engine.
- Store or delete retained messages.
- Fan out messages to all matching regular subscribers.
- Apply per-subscription delivery rules (QoS downgrade, No Local,
  Retain As Published, Subscription Identifier).
- Buffer messages for offline subscribers and deliver them on reconnect.
- Discard messages whose Message Expiry Interval has elapsed.
- Deliver exactly one message per shared subscription group via round-robin.

---

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `message_router_error.h`           | 12    | `MessageRouterError` enum and `MessageRouterException` |
| `inbound_publish_processor.h/.cpp` | 12.1  | Pre-processes inbound PUBLISH: alias resolution, authz, retain, subscriber lookup |
| `subscriber_fanout.h/.cpp`         | 12.2  | Applies per-subscription rules (QoS downgrade, No Local, Retain As Published, Subscription Identifier) |
| `offline_queue.h/.cpp`             | 12.3  | Per-client FIFO message buffer for disconnected sessions |
| `message_expiry_controller.h/.cpp` | 12.4  | Enforces Message Expiry Interval; discards expired messages and updates remaining time |
| `shared_subscription_dispatcher.h/.cpp` | 12.5 | Round-robin delivery for MQTT 5.0 shared subscription groups |
| `message_router.h/.cpp`            | 12    | Top-level coordinator; combines all sub-modules |

---

## Public API

### `MessageRouterError` / `MessageRouterException`

```cpp
enum class MessageRouterError : uint8_t {
    PublishNotAuthorized,
    TopicAliasInvalid,
    QueueFull,
};
```

---

### `InboundPublishProcessor` (12.1)

```cpp
InboundPublishProcessor(AclEngine&, RetainedMessageStore&, SubscriptionStore&);

[[nodiscard]] std::vector<MatchResult> process(
    Message& msg, std::string_view client_id,
    std::string_view username, TopicAliasTable& alias_table);

[[nodiscard]] std::vector<Message> retained_for_filter(
    std::string_view topic_filter) const;
```

**`process`**: resolves optional Topic Alias in-place, checks publish ACL
(throws `PublishNotAuthorized` on denial), stores retained message if
`msg.retain` is set, returns matching subscriber list.

---

### `SubscriberFanout` (12.2)

```cpp
[[nodiscard]] static std::vector<DeliveryItem> prepare(
    const Message& msg,
    const std::vector<MatchResult>& subscribers,
    std::string_view publisher_client_id);
```

Stateless.  For each subscriber: applies No Local filter, QoS downgrade,
Retain As Published, and Subscription Identifier injection.

Also exposes `apply_subscription_rules(msg, sub)` for single-subscription
delivery paths such as retained delivery on subscribe.

---

### `OfflineQueue` (12.3)

```cpp
OfflineQueue(std::size_t max_size = k_default_max_size);

void enqueue(std::string_view client_id, const Message& msg);
[[nodiscard]] std::vector<QueuedMessage> drain(std::string_view client_id);
[[nodiscard]] std::size_t               size(std::string_view client_id) const noexcept;
void                                     purge(std::string_view client_id);
```

`enqueue` throws `MessageRouterException(QueueFull)` when the per-client
limit is exceeded.  `drain` clears the queue and returns all buffered
`QueuedMessage` values in FIFO order.

---

### `MessageExpiryController` (12.4)

```cpp
[[nodiscard]] static bool update_expiry(
    Message& msg,
    std::chrono::steady_clock::time_point enqueue_time,
    std::chrono::steady_clock::time_point now = steady_clock::now());
```

Stateless.  Returns `false` when the message has expired (must be
discarded).  When valid, reduces the `MessageExpiryInterval` property
to the remaining lifetime in seconds.

---

### `SharedSubscriptionDispatcher` (12.5)

```cpp
void add_member(std::string_view group, std::string_view topic_filter,
                std::string_view client_id, const Subscription& sub);
void remove_member(std::string_view group, std::string_view topic_filter,
                   std::string_view client_id);
void remove_client(std::string_view client_id);

[[nodiscard]] std::vector<MatchResult>
select_next_for_topic(std::string_view topic_name);

[[nodiscard]] std::size_t member_count(std::string_view group,
                                        std::string_view topic_filter) const noexcept;
```

Groups are keyed by `(group, topic_filter)`.  `select_next_for_topic`
performs MQTT 5.0 topic filter matching and round-robin selection.
Empty groups are removed automatically.

---

### `MessageRouter` (coordinator)

```cpp
using DeliverFn  = std::function<void(std::string_view, const Message&)>;
using IsOnlineFn = std::function<bool(std::string_view)>;

MessageRouter(InboundPublishProcessor&, OfflineQueue&,
              SharedSubscriptionDispatcher&, IsOnlineFn, DeliverFn);

void route(Message& msg, std::string_view client_id,
           std::string_view username, TopicAliasTable& alias_table);

void flush_offline_queue(std::string_view client_id,
                          std::chrono::steady_clock::time_point now = ...);

void deliver_retained(std::string_view client_id,
                      std::string_view topic_filter,
                      const Subscription& subscription,
                      bool is_new_subscription,
                      std::chrono::steady_clock::time_point now = ...);
```

`route` runs the full pipeline: pre-process → shared dispatch → fanout →
expiry check → online deliver / offline enqueue.

`flush_offline_queue` drains buffered messages for a reconnecting client,
discards any that have expired, and delivers the rest via `DeliverFn`.

`deliver_retained` delivers retained messages for a newly stored subscription,
respecting Retain Handling and applying the same subscription-level message
adjustments used in normal fanout.
