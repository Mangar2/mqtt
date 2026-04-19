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
- Trigger retained delivery for newly accepted non-shared subscriptions.
- Build SUBACK/UNSUBACK reason code vectors with one result per filter.

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `subscription_orchestrator.h/.cpp` | 19 | `SubscriptionOrchestrator` — SUBSCRIBE/UNSUBSCRIBE orchestration and validation |
