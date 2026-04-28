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
