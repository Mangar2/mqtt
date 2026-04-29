# YAHA Reimplementation Journal

## 2026-04-28

### [ARTIFACT] .claude/commands/yaha-spec.md created
New skill created to guide step-by-step specification of YAHA components being reimplemented in C++. Defines workflow: read legacy code, identify cross-cutting concerns, write spec, review, output summary.

### [ARTIFACT] spec/yaha/SPEC-IMqttComponent.md created
Identified as cross-cutting concern during MessageStore spec work. Defines the pure virtual interface (`getSubscriptions`, `handleMessage`, `setPublishCallback`) that decouples any component from the MQTT transport layer. Applies to all future components.

### [ARTIFACT] spec/yaha/SPEC-messagetree.md created
Created as standalone spec for the internal MessageTree data structure used by MessageStore.

### [ARTIFACT] spec/yaha/SPEC-messagestore.md created
First component spec. Covers MQTT subscriptions, HTTP query interface, message tree data model, persistence, configuration, and lifecycle.

### [CORRECTION] SPEC-messagetree.md deleted, content merged into SPEC-messagestore.md
MessageTree is only used by MessageStore — not a cross-cutting concern. A standalone spec was the wrong structure. On user feedback, all content moved into SPEC-messagestore.md (Data model, Configuration, Architectural notes, Open questions sections). Skill updated: dedicated spec only for concerns used by more than one component.

### [DECISION] $SYS topics renamed to $MONITORING
MQTT specification reserves the $SYS prefix for broker-originated messages. Components must never publish or subscribe to $SYS topics. On user instruction: all legacy $SYS/... topics become $MONITORING/... in new specs. SPEC-messagestore.md corrected at all three occurrences. Skill section "MQTT topic conventions" added.

### [INSIGHT] Message identified as central shared data type
User prompted a review of whether the Message format deserves its own spec. Cross-check across all legacy components (valueservice, raspberry, zwave, pushover, serialdevice, and others) confirmed that `{topic, value, reason, qos, retain}` with the ReasonEntry chain is the universal data carrier of the system. SPEC-message.md created. Skill Step 2 updated to explicitly check shared data formats (not just interfaces) as a cross-cutting concern trigger.

### [ARTIFACT] .claude/commands/yaha-log.md created
New skill created to define when and how the AI maintains the journal. Specifies entry types (ARTIFACT, MILESTONE, DECISION, CORRECTION, INSIGHT), format, and integration with other yaha skills.

### [ARTIFACT] spec/yaha/JOURNAL.md created
This file. Retroactively populated with all events from the initial session.

## 2026-04-28 (continued)

### [DECISION] HTTP-MQTT bridge as separate executable, not broker feature
User asked whether the HTTP interface for simple microcontrollers should be built into yahabroker or as a separate program. Decision: separate executable. yahabroker must not carry HTTP protocol logic; the bridge connects to it as a regular MQTT client. Follows YAHA standalone-program pattern. yahabroker has zero knowledge of the bridge.

### [ARTIFACT] spec/yaha/SPEC-http-mqtt-bridge.md created
Specifies the HTTP-MQTT bridge: HTTP server for microcontroller commands (connect, publish, subscribe, unsubscribe, disconnect, pubrel), HTTP callbacks to devices for message delivery, MQTT client to yahabroker for real MQTT transport, session and queue management, persistence.

### [CORRECTION] SPEC-http-mqtt-bridge.md rewritten: thin bridge, one MQTT connection per device
Analysis of MqttClient showed it handles reconnect, re-subscribe, and keep-alive automatically. On user direction: bridge delegates all session/QoS/queue/persistence concerns to yahabroker by opening one real MQTT connection per device. Bridge holds only in-memory device registry (clientId, host, port, mqtt instance). No persistence, no session state, no queue management in the bridge itself.

### [DECISION] cpp-httplib added to CMakeLists.txt via FetchContent
Multiple YAHA client executables (MessageStore, HTTP-MQTT Bridge, others) need an HTTP server/client. yahabroker does not. cpp-httplib (header-only, MIT) added as shared FetchContent dependency. Individual executables add it to their include path; yahabroker is unaffected.

### [DECISION] HTTP-MQTT Bridge: three design decisions closed by user
No WebSocket support — HTTP only between bridge and devices. Failed delivery is silent: device removed, MQTT connection closed, no separate notification. Authentication by yahabroker only — bridge forwards credentials as-is, performs no validation itself.

### [ARTIFACT] spec/yaha/IMPL-messagestore.md created
Implementation plan for MessageStore in 10 steps across 4 phases. Phase 1 (steps 1–3) produces the shared foundations reused by all future YAHA MQTT services: Message type, IMqttComponent interface, YahaMqttClient session. Phase 2 (steps 4–7) builds the MessageStore component: message tree, persistence, component logic, HTTP interface. Phase 3 (step 8) composes the executable. Phase 4 (steps 9–10) covers unit and integration verification.

### [MILESTONE] Step 1 complete — Message type implemented and tested
Created `src/yaha/message/message.h`, `message.cpp`, `test/message_test.cpp`. Value type with `std::variant<std::string, double>`, `Qos` enum, `ReasonEntry` struct, `isOn()`, `addReason()` (prepend, most-recent-first), `clone()`, and `validate()`. 58 assertions, 20 test cases, all pass. Build: zero warnings, zero errors. SPEC.md files created at `src/yaha/` and `src/yaha/message/`; `src/SPEC.md` updated with `yaha/` entry.

### [DECISION] reason chain: prepend (most-recent at index 0)
Legacy JS code used `Array.push` (most-recent at end). Written spec says "most recent is at the front". C++ implementation follows the spec: `addReason` inserts at `begin()`, so `reason()[0]` is always the newest entry.

## 2026-04-29

### [ARTIFACT] src/yaha/mqtt_component/* created (Step 2)
Implemented `IMqttComponent` as transport boundary interface in `src/yaha/mqtt_component/mqtt_component.h` and `mqtt_component.cpp`. Added shared aliases `SubscriptionMap` and `PublishCallback`, pure virtual methods `getSubscriptions()` and `handleMessage()`, optional default `setPublishCallback(...)`, and virtual destructor.

### [ARTIFACT] src/yaha/mqtt_component/test/* created
Added `TEST_SPEC.md` and `mqtt_component_test.cpp` with contract-focused tests for default callback setter, polymorphic dispatch, subscription map exposure, and overridden publish callback behavior.

### [MILESTONE] Step 2 complete — IMqttComponent implemented and tested
Step 2 from `spec/yaha/IMPL-messagestore.md` is complete. `src/yaha/SPEC.md` updated with the new `mqtt_component/` sub-module entry. Local compile and focused tests pass.

### [ARTIFACT] src/yaha/mqtt_client/* created (Step 3)
Implemented `YahaMqttClient` in `src/yaha/mqtt_client/mqtt_client.h` and `mqtt_client.cpp` as the reusable MQTT session driver for any `IMqttComponent`. Added callback-based transport abstraction, background session loop, reconnect with subscription replay, inbound dispatch with topic-filter matching (`+`, `#`), keep-alive ping scheduling, publish forwarding, and graceful shutdown.

### [ARTIFACT] src/yaha/mqtt_client/test/* created
Added `TEST_SPEC.md` and `mqtt_client_test.cpp` with behavior tests for callback injection order, initial subscription setup, reconnect+resubscribe, publish forwarding, inbound topic filtering, keep-alive ping behavior, and close/disconnect semantics.

### [MILESTONE] Step 3 complete — YahaMqttClient implemented and validated
Step 3 from `spec/yaha/IMPL-messagestore.md` is implemented under `src/yaha/mqtt_client/`. Full `python3 test/run_coverage.py` passed all tests (1269/1269). Scoped coverage for `src/yaha/` reports threshold MET with `mqtt_client.cpp` at Regions 91.84%, Functions 93.75%, Lines 89.02%.
