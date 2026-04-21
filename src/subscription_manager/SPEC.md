# subscription_manager — Subscription Orchestration (Module 19)

Encapsulates SUBSCRIBE/UNSUBSCRIBE orchestration logic that was previously in Broker.
Depends on: `authz/`, `store/`, `message_router/`, `topic/`, `data_model/`.

## Responsibilities

- Parse and validate shared subscription filters (`$share/<group>/<topic_filter>`).
- Enforce SUBSCRIBE protocol constraints:
  - Subscription Identifier must be non-zero when present.
  - No Local is invalid on shared subscriptions.
- Validate topic filters and apply ACL subscribe checks.
- Store normal subscriptions in `SubscriptionStore`.
- Register and remove shared subscription members in `SharedSubscriptionDispatcher`.
- Synchronize successful non-shared subscribe/unsubscribe changes into
  `SessionStore` snapshot state (`SessionState.subscriptions`) so persistence
  reflects the durable subscription set.
- Trigger retained delivery for newly accepted non-shared subscriptions.
- Build SUBACK/UNSUBACK reason code vectors with one result per filter.

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `subscription_orchestrator.h/.cpp` | 19 | `SubscriptionOrchestrator` — SUBSCRIBE/UNSUBSCRIBE orchestration and validation |

## Public API

### `SubscriptionOrchestrator` (subscription_orchestrator.h)

```cpp
SubscriptionOrchestrator(SubscriptionStore&, SessionStore&,
                         SharedSubscriptionDispatcher&, MessageRouter&,
                         TopicValidator&, AuthzService&);

std::vector<ReasonCode> handle_subscribe(std::string_view client_id,
                                         const SubscribePacket& packet);
std::vector<ReasonCode> handle_unsubscribe(std::string_view client_id,
                                           const UnsubscribePacket& packet);

// Write-through callback (13 — persistence)
void set_on_session_changed(std::function<void()> callback) noexcept;
```

The `set_on_session_changed` callback fires after any successful subscribe or
unsubscribe that updates the durable `SessionStore` snapshot.  Exceptions from
the callback are swallowed so they never propagate into the hot path.
