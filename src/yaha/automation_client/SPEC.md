# automation_client — YAHA Automation Runtime Composition and Rule Sync

## Purpose

Provides YAHA-standard standalone runtime mapping and component behavior for
Automation rule synchronization with FileStore and MQTT rule-management topics.

## Public API

### Struct `AutomationClientConfig`

| Field | Type | Default | Notes |
|------|------|---------|-------|
| `rulesKeyPath` | `std::string` | `/automation/rules` | FileStore key path for full rules tree |
| `fileStoreHost` | `std::string` | `127.0.0.1` | FileStore HTTP host |
| `fileStorePort` | `std::uint16_t` | `8210` | FileStore HTTP port |
| `fileStoreEnabled` | `bool` | `true` | Enables startup load and write-back |
| `fileStoreStartupRetryCount` | `std::uint32_t` | `10` | Additional startup retries after first failed FileStore load |
| `fileStoreStartupRetryIntervalSeconds` | `std::uint32_t` | `60` | Wait interval between startup retries |
| `monitorTopicPrefix` | `std::string` | `$MONITOR/FileStore` | Monitoring subscription prefix |
| `managementTopicPrefix` | `std::string` | `$MONITOR/automation/rules` | Runtime update prefix |
| `presenceTopic` | `std::string` | `$MONITOR/presence` | Presence variable bootstrap topic |
| `motionTopics` | `std::vector<std::string>` | defaults | Static motion/control topic filters |
| `longitude` | `double` | `0.0` | Geo longitude for internal variables |
| `latitude` | `double` | `0.0` | Geo latitude for internal variables |
| `subscribeQos` | `Qos` | `Qos::AtLeastOnce` | Subscription qos for monitor/management channels |
| `logIncomingMessages` | `bool` | `false` | Logs incoming MQTT messages handled by automation client |
| `logOutgoingMessages` | `bool` | `false` | Logs outgoing MQTT messages emitted by automation client |

### Class `AutomationClientComponent` : `IMqttComponent`

| Member | Signature | Notes |
|--------|-----------|-------|
| ctor | `explicit AutomationClientComponent(AutomationClientConfig)` | Stores configuration |
| `getSubscriptions` | `SubscriptionMap() const` | Subscribes monitoring, management, static motion topics, and rule-discovered dynamic variable topics |
| `handleMessage` | `void(const Message&)` | Handles monitor reload, rule management, and domain message rule evaluation |
| `run` | `void()` | Loads rules from FileStore startup key path |
| `close` | `void()` | Stops lifecycle flag |
| `setPublishCallback` | `void(PublishCallback)` | Stores callback for management ack publishes |
| `ruleCount` | `size_t() const` | Test/diagnostic helper |
| `hasRule` | `bool(const std::string&) const` | Test/diagnostic helper |

### Struct `AutomationClientRuntimeConfig`

| Field | Type | Notes |
|------|------|-------|
| `automationConfig` | `AutomationClientConfig` | Automation component settings |
| `mqttConfig` | `YahaMqttClient::Config` | Generic MQTT runtime settings |

### Config mapping helpers

| Function | Signature | Notes |
|---------|-----------|-------|
| `tryLoadAutomationClientConfigFromIni` | `(const IniDocument&, AutomationClientConfig&, std::string&) -> bool` | Maps automation + filestore fields; supports legacy `monitoring.topicPrefix` fallback and uses shared message-log INI helper for `automation.logIncomingMessages` / `automation.logOutgoingMessages` |
| `tryLoadAutomationClientRuntimeConfigFromIni` | `(const IniDocument&, AutomationClientRuntimeConfig&, std::string&) -> bool` | Maps full runtime config |

INI error handling:
- Invalid recoverable values do not abort config loading.
- For invalid recoverable values, loader writes deterministic warning logs to
  `std::cerr` and keeps the current default value.
- `filestore.startupRetryCount` uses `0..uint32_max`.
- `filestore.startupRetryIntervalSeconds` uses `1..uint32_max`.
- Runtime config load does not fail when MQTT/message-log sub-loaders reject one
  value; defaults are kept with warning context.

## Behavior

- Startup (`run`) performs FileStore GET on `rulesKeyPath` when enabled.
- Startup initializes presence variable bootstrap value `initial` on `presenceTopic`.
- Startup also initializes legacy-compatible alias `status/presence` with value `initial`.
- Monitoring updates:
  - Subscribed topic prefix `$MONITOR/FileStore/#` (configurable).
  - On payload with matching `keyPath` equal to `rulesKeyPath`, rules are reloaded from FileStore.
  - Reload failures emit structured error logs with operation and key path.
- Dynamic variable subscriptions:
  - On each rule-tree load/update, the component parses expression snippets and extracts external variables.
  - Extracted variable topics are added to subscription map with `subscribeQos`.
- Runtime management updates:
  - Subscribed topic prefix `$MONITOR/automation/rules/#` (configurable).
  - Supported command topic shape: `<managementTopicPrefix>/<ruleName>/set`.
  - Payload `delete` removes rule.
  - JSON payload sets/updates rule object under `rules.<ruleName>`.
  - Update/delete uses transactional staging: persist staged full tree first, then commit in-memory state and dynamic subscriptions.
  - On persistence failure, in-memory rule tree and subscriptions stay unchanged.
  - Ack publish topic: `<managementTopicPrefix>/<ruleName>` with qos 1 and payload one of
    `updated`, `deleted`, `validation_failed`, or `persist_failed`.
- Runtime rule evaluation:
  - For each non-control incoming message, the component updates runtime variable map `variable(topic)=value`.
  - Domain-message ingestion maintains event history with two classes:
    - motion events as bounded ordered history (max 100, trim oldest 20 on overflow)
    - non-motion events as one-cycle topic set
  - Incoming domain messages with payload value `0` are ignored for event-history insertion.
  - It builds evaluation context from runtime variables plus internal variables (`/time`, `/weekday`, sun/twilight).
  - It always pre-populates `/time` and `/weekday` from current evaluation timestamp before sun/twilight calculation, so rules using `/time` never fail due to sun-event calculation errors.
  - It processes complete rule tree with runtime gates before rule execution:
    - `active` boolean skip gate
    - weekday gate (`weekdays`, evaluated in local calendar weekday)
    - time window gate (`time` + optional `duration`, parsed against local day start)
    - event gates (`anyOf`, `allOf`, `noneOf`, `allow`)
    - inactivity gate (`durationWithoutMovementInMinutes`)
  - Triggered rule candidates are then filtered by delivery controls:
    - dedup on identical topic/value output while rule stays active
    - `delayInSeconds` stable-candidate gate
    - `cooldownInSeconds` periodic resend gate for identical output
  - Produced rule output messages are published via delivery-result callback.
  - Explicit callback failure results (`PublishResult::fail(...)`) are treated as outbound send failures and enter the same retry queue as thrown publish errors.
  - Produced outputs are reflected back into runtime variable map.
  - Failed outbound sends are added to a bounded retry queue and retried on subsequent component message handling cycles.
  - Retries stop after max attempt budget and emit explicit exhaustion error logs.
- Rule debug trace requests:
  - Subscribed via static automation monitor namespace shape
    `$MONITOR/automation/<rule-link>/debug`.
  - On request, the component resolves exactly one rule referenced by `<rule-link>`
    (supports links with or without leading `rules/`) and publishes a trace
    response message to `$MONITOR/automation/<rule-link>/trace`.
  - Trace response payload is one of `triggered`, `not_triggered`, or `error`.
  - Trace response `reason` chain is compact and explanation-focused using
    informational lines in form `<name>: passed|failed (...)` where applicable.
  - Explain entries include explicit `check: passed|failed (...)` and
    `value: ... (evaluation result)` lines.
  - Error entries use `error: ...` without additional debug prefixes.
  - Final debug decision and `would send=<N>` preview are computed through the
    same runtime engine path used by live rule evaluation, including all runtime
    gates (`active`, `weekdays`, `time`/`duration`, `anyOf`/`allOf`/`noneOf`/
    `allow`, inactivity) and delivery controls (`delayInSeconds`,
    `cooldownInSeconds`, dedup state).
  - Runtime gate decisions are appended without transport/debug prefixes
    (for example `active: passed (active == true)`).
  - Optional gate lines are omitted when a gate is not configured
    (for example no `time` line when rule has no `time` field).
  - For `triggered` responses, trace reasons include both the rule-output
    candidate count and runtime delivery outcome (`would send=<N>`) after
    dedup/delay/cooldown evaluation using current runtime delivery state.
  - Explain summary labels use the resolved rule identifier/path; output topic is
    only a fallback when no rule identifier is available.
  - Runtime/internal variable snapshots are intentionally not emitted in trace reasons.
  - Raw trace payload serialization JSON-escapes control characters
    (including newline/tab/carriage-return and other ASCII control codes)
    to keep forwarded envelopes parseable by downstream consumers.
  - Trace payload envelope serialization is delegated to shared message utility
    `yaha/message/message_payload_codec.*`.
- Logging behavior:
  - If `logIncomingMessages=true`, each inbound message handled by component is logged through shared message logging service (`yaha/message/message_log_service.*`).
  - If `logOutgoingMessages=true`, each outbound rule/ack message is logged through shared message logging service only after successful callback send.
  - Shared log lines use deterministic field order and include full structured reason chain.
  - Outbound rule message reasons use the rule identifier (for example rule name/path)
    rather than the emitted topic name when available.
  - Failed outbound sends are logged as shared structured outgoing lines with `event=publish_failed`, `category=<publish-failure-category>`, and escaped failure reason detail.
  - FileStore GET/POST failures and internal-variable calculation failures emit structured `automation_client[error]` lines.
  - Internal-variable calculation failures include escaped exception detail text
    (for example unresolved required sunrise/sunset cause context from
    automation internal variable calculation).
  - Undefined twilight-only conditions no longer emit this error path because
    twilight events fall back to sunrise/sunset values inside automation
    internal variable calculation.

## Files

| File | Role |
|------|------|
| `automation_client_component.h` | IMqttComponent declarations for automation rule sync |
| `automation_client_component.cpp` | Runtime behavior implementation |
| `automation_control_topics.h/.cpp` | Topic classification and rule-link extraction helper |
| `automation_rule_json.h/.cpp` | Rule JSON parse/serialize helper |
| `automation_message_values.h/.cpp` | Message value conversion and logging text helper |
| `automation_rule_lookup.h/.cpp` | Rule-link path lookup helper |
| `automation_rule_tree_access.h/.cpp` | Rule-tree object and string-field access helper |
| `automation_trace_format.h/.cpp` | Debug-trace formatting helper |
| `automation_publish_failure_text.h/.cpp` | Publish failure-category text mapping helper |
| `automation_client_app.h` | Runtime config mapping declarations |
| `automation_client_app.cpp` | Runtime config mapping implementation |
| `rule_runtime_engine.h/.cpp` | Runtime rule-gate, event-history, and delivery-control helper |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/automation_client_component_test.cpp` | Component unit tests |
| `test/automation_client_rule_runtime_test.cpp` | Runtime gate and delivery-control unit tests |
| `test/automation_client_app_test.cpp` | Config mapping unit tests |
