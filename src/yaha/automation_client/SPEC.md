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
| `monitorTopicPrefix` | `std::string` | `$MONITOR/FileStore` | Monitoring subscription prefix |
| `managementTopicPrefix` | `std::string` | `$MONITORING/automation/rules` | Runtime update prefix |
| `presenceTopic` | `std::string` | `$MONITORING/presence` | Presence variable bootstrap topic |
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
| `tryLoadAutomationClientConfigFromIni` | `(const IniDocument&, AutomationClientConfig&, std::string&) -> bool` | Maps automation + filestore fields; supports legacy `monitoring.topicPrefix` fallback |
| `tryLoadAutomationClientRuntimeConfigFromIni` | `(const IniDocument&, AutomationClientRuntimeConfig&, std::string&) -> bool` | Maps full runtime config |

## Behavior

- Startup (`run`) performs FileStore GET on `rulesKeyPath` when enabled.
- Startup initializes presence variable bootstrap value `initial` on `presenceTopic`.
- Monitoring updates:
  - Subscribed topic prefix `$MONITOR/FileStore/#` (configurable).
  - On payload with matching `keyPath` equal to `rulesKeyPath`, rules are reloaded from FileStore.
- Dynamic variable subscriptions:
  - On each rule-tree load/update, the component parses expression snippets and extracts external variables.
  - Extracted variable topics are added to subscription map with `subscribeQos`.
- Runtime management updates:
  - Subscribed topic prefix `$MONITORING/automation/rules/#` (configurable).
  - Supported command topic shape: `<managementTopicPrefix>/<ruleName>/set`.
  - Payload `delete` removes rule.
  - JSON payload sets/updates rule object under `rules.<ruleName>`.
  - After successful update/delete, full rule tree is persisted to FileStore via HTTP POST.
  - Ack publish topic: `<managementTopicPrefix>/<ruleName>` with qos 1 and payload one of
    updated rule JSON, `invalid rule`, or `deleted`.
- Runtime rule evaluation:
  - For each non-control incoming message, the component updates runtime variable map `variable(topic)=value`.
  - It builds evaluation context from runtime variables plus internal variables (`/time`, `/weekday`, sun/twilight).
  - It processes complete rule tree with `RulesTreeProcessor`.
  - Produced rule output messages are published via callback.
  - Produced outputs are reflected back into runtime variable map.
- Logging behavior:
  - If `logIncomingMessages=true`, each inbound message handled by component is logged.
  - If `logOutgoingMessages=true`, each outbound rule/ack message sent via callback is logged.

## Files

| File | Role |
|------|------|
| `automation_client_component.h` | IMqttComponent declarations for automation rule sync |
| `automation_client_component.cpp` | Runtime behavior implementation |
| `automation_client_app.h` | Runtime config mapping declarations |
| `automation_client_app.cpp` | Runtime config mapping implementation |
| `test/TEST_SPEC.md` | Unit test specification |
| `test/automation_client_component_test.cpp` | Component unit tests |
| `test/automation_client_app_test.cpp` | Config mapping unit tests |
