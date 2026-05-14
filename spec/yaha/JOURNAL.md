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

### [DECISION] $SYS topics renamed to $MONITOR
MQTT specification reserves the $SYS prefix for broker-originated messages. Components must never publish or subscribe to $SYS topics. On user instruction: all legacy $SYS/... topics become $MONITOR/... in new specs. SPEC-messagestore.md corrected at all three occurrences. Skill section "MQTT topic conventions" added.

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

### [ARTIFACT] src/yaha/message_store/* created (Step 4)
Implemented MessageTree in `src/yaha/message_store/message_tree.h` and `message_tree.cpp` with topic-segment indexing, `addData`, `getSection`, `getNodes`, and `cleanup`. Added bounded history with hysteresis trimming, internal history compression (consecutive equal value/reason buckets), and decompression on query output.

### [ARTIFACT] src/yaha/message_store/test/* created
Added `TEST_SPEC.md` and `message_tree_test.cpp` covering node insertion, update->history transfer, bounded trim behavior, depth-limited section retrieval, reason/history projection flags, snapshot diff mode, stale cleanup with branch pruning, and structural (non-wildcard) topic-prefix query behavior.

### [MILESTONE] Step 4 complete — MessageTree implemented and validated
Step 4 from `spec/yaha/IMPL-messagestore.md` is implemented under `src/yaha/message_store/`. Full `python3 test/run_coverage.py` is green on tests (1277/1277). Scoped coverage for `src/yaha/` reports threshold MET with `message_tree.cpp` at Regions 84.62%, Functions 95.00%, Lines 84.80%.

### [ARTIFACT] src/yaha/message_store/message_tree_persistence.* created (Step 5)
Implemented `MessageTreePersistence` with immediate snapshot save, periodic snapshot loop, restore from newest valid snapshot, and retention cleanup of older files. Snapshot format includes topic/value/time/reason/history for full tree reconstruction.

### [ARTIFACT] MessageTree restore API extended for persistence
Added `MessageTree::replaceAllNodes(...)` for startup restore and `compressHistory(...)` helper so persisted decompressed history can be reconstructed into internal compressed representation.

### [ARTIFACT] src/yaha/message_store/test/message_tree_persistence_test.cpp created
Added persistence unit tests for roundtrip restore, corrupt-file fallback, malformed snapshot handling, missing snapshot handling, retention modes (`keepFiles > 0` and `keepFiles == 0`), periodic loop behavior, and default-constructor path.

### [MILESTONE] Step 5 complete — MessageStore persistence implemented and validated
Step 5 from `spec/yaha/IMPL-messagestore.md` is implemented under `src/yaha/message_store/`. Full `python3 test/run_coverage.py` passed tests (1287/1287). Scoped coverage for `src/yaha/` reports threshold MET with `message_tree_persistence.cpp` at Regions 84.85%, Functions 100.00%, Lines 80.84%.

### [ARTIFACT] src/yaha/message_store/message_store.* created (Step 6)
Implemented `MessageStore` component logic in `message_store.h/.cpp` as `IMqttComponent`: configured subscription exposure, inbound message dispatch to `MessageTree`, cleanup-topic handling (`$MONITOR/messages/cleanup`), lifecycle `run()`/`close()`, startup restore via persistence, periodic persistence start/stop, and final persist on shutdown.

### [ARTIFACT] src/yaha/message_store/test/message_store_test.cpp created
Added component tests for configured subscriptions, regular message storage, numeric cleanup dispatch, invalid cleanup payload behavior, run/close lifecycle callbacks, restore-on-run behavior, periodic persistence start, final persist on close, and idempotent run/close semantics.

### [MILESTONE] Step 6 complete — MessageStore component logic implemented and validated
Step 6 from `spec/yaha/IMPL-messagestore.md` is implemented under `src/yaha/message_store/`. Full `python3 test/run_coverage.py` passed tests (1294/1294). Scoped coverage for `src/yaha/` reports threshold MET with `message_store.cpp` at Regions 93.33%, Functions 100.00%, Lines 91.86%.

## 2026-04-30

### [ARTIFACT] spec/yaha/SPEC-http-mqtt-interface.md created
Created a dedicated YAHA spec for the HTTP-modified MQTT interface in version 1.0 based on the legacy source under `spec/@mangar2/mqtt-utils/src/mqtt-interface/`. The spec documents request/response contracts, headers, payload shapes, and validation behavior for connect, disconnect, publish, pubrel, subscribe, and unsubscribe.

### [MILESTONE] HTTP-modified MQTT interface 1.0 spec completed
Version 1.0 is now documented as a standalone source-of-truth spec for YAHA. Version 0.0 was intentionally excluded as requested.

### [ARTIFACT] spec/yaha/IMPL-broker-connector.md created
Created implementation plan for a YAHA Broker Connector that subscribes on an HTTP MQTT interface source broker and forwards all received messages to a standard MQTT receiver broker. Plan includes architecture split, phased implementation order, configuration model, reliability rules, and verification steps.

### [MILESTONE] Broker Connector plan completed
A full step-by-step delivery plan now exists for the new connector component, including open decisions for QoS, retain handling, topic policy, and failure strategy before coding starts.

### [DECISION] Broker Connector boundary clarified
The connector plan now explicitly fixes architecture boundaries: receiver side uses the existing generic YAHA MQTT client unchanged, while the HTTP source connection and relay policy are the connector domain part (fachteil). This avoids creating a second custom MQTT client implementation inside the connector.

### [ARTIFACT] spec/yaha/SPEC-broker-connector.md created
Created the component specification for Broker Connector with purpose, system role, standalone composition, subscriptions, publish behavior, configuration, error handling, and architecture constraints. The spec fixes one-way relay behavior from HTTP source broker to standard MQTT receiver broker.

### [ARTIFACT] spec/yaha/SPEC-broker-connector-contracts.md created
Created Phase 1 contract specification for SourceHttpBrokerAdapter, ReceiverPublishPort, BrokerConnectorComponent, ConnectorRuntimePort, and shared data/config contracts. This defines stable boundaries before implementation and keeps transport concerns separated from relay domain logic.

### [MILESTONE] Broker Connector Phase 1 completed
Phase 1 from the implementation plan is now implemented through component spec and explicit adapter contracts. The architecture boundary between standard YAHA MQTT client usage and connector HTTP fachteil is now normative in the specs.

## 2026-05-01

### [ARTIFACT] src/yaha/broker_connector/* created
Implemented Broker Connector Phase 2 source-side module under `src/yaha/broker_connector/` with `SourceHttpBrokerAdapter` and `SourceLifecycleManager`. The adapter handles HTTP MQTT 1.0 connect/subscribe/ping/disconnect plus `/publish` and `/pubrel` callback acks; lifecycle manager adds reconnect and re-subscribe loop behavior.

### [ARTIFACT] src/yaha/broker_connector/test/* created
Added unit test specification and source-side tests for connect/subscribe callback flow, qos2 pubrel handshake, reconnect behavior, qos0 metadata path, invalid publish payload handling, and disconnected ping guard behavior.

### [ARTIFACT] src/yaha/SPEC.md updated
Added `broker_connector/` sub-module entry to the YAHA source index so module documentation stays discoverable from the top-level YAHA spec map.

### [MILESTONE] Broker Connector Phase 2 implemented
Phase 2 implementation from `spec/yaha/IMPL-broker-connector.md` is now in place: source adapter and source lifecycle manager are implemented with tests and integrated project documentation.

### [ARTIFACT] src/yaha/broker_connector/receiver_publish_port.* created
Implemented receiver-side publish boundary (`ReceiverPublishPort`) and MQTT-backed adapter (`ReceiverMqttPublishPort`) using the standard `YahaMqttClient` without connector-specific transport forks. Added testable transport injection constructor for deterministic unit tests.

### [ARTIFACT] src/yaha/broker_connector/relay_component.* created
Implemented `BrokerConnectorComponent` with source-to-receiver forwarding logic, bounded publish retry policy, qos/retain mapping, and runtime counters (`received`, `forwarded`, `failed`).

### [ARTIFACT] src/yaha/broker_connector/test/relay_component_test.cpp created
Added phase 3 unit tests for receiver publish port runtime behavior and relay component forwarding/retry/counter behavior, including edge branches for not-running guard, idempotent start, pre-start publish rejection, and non-normalized qos policy.

### [ARTIFACT] src/yaha/broker_connector/SPEC.md updated
Extended module spec from phase 2 source-only scope to phase 3 scope with receiver publish contracts, relay policy/counter contracts, and updated runtime behavior documentation.

### [MILESTONE] Broker Connector Phase 3 implemented
Phase 3 from `spec/yaha/IMPL-broker-connector.md` is now implemented: standard YAHA MQTT client is wired through receiver publish port and relay component forwards source callbacks with retry and statistics. Full `python3 test/run_coverage.py` passed tests (1335/1335); changed production files exceed 80% on Regions/Functions/Lines.

### [ARTIFACT] src/yaha/broker_connector_client/* created
Implemented phase 4 composition module with runtime config mapping (`broker_connector_client_app.*`) and runtime orchestration (`broker_connector_runtime.*`) for deterministic startup/shutdown order.

### [ARTIFACT] src/yaha_brokerconnectorclient_main.cpp created
Added standalone broker connector executable main composition: CLI/config load, runtime wiring (source adapter + lifecycle, relay component, receiver publish port), signal-driven runtime execution.

### [ARTIFACT] CMake and README updated for BrokerConnector executable
Registered new `yahabrokerconnectorclient` target in `CMakeLists.txt`, added install rule, and documented run/config usage in `README.md`.

## 2026-05-13

### [ARTIFACT] HTTP MQTT interface client logging updated
Updated `src/yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.cpp` so outgoing broker publish logs are emitted only after broker publish callback success (`broker_publish_ack`). Added explicit `broker_publish_failed` error logging with full message fields (`topic`, `qos`, `retain`, `dup`, optional `packetid`, `value`) and no-ACK detail marker.

### [ARTIFACT] Broker transport publish now waits for ACK
Updated `src/yaha/mqtt_client/broker_transport.cpp` to wait for broker acknowledgements on QoS1/QoS2 publishes (`PUBACK`, `PUBREC`, `PUBCOMP`) and throw on timeout or write failure. This enables reliable detection and logging of missing broker ACK scenarios in client runtimes.

### [ARTIFACT] HTTP MQTT interface client tests/spec updated
Extended `src/yaha/http_mqtt_interface_client/test/http_mqtt_interface_client_app_test.cpp` and `test/TEST_SPEC.md` with a missing-ACK error logging test and adjusted success-log expectations to ACK-gated logging. Updated module specs in `src/yaha/http_mqtt_interface_client/SPEC.md`, `src/yaha/mqtt_client/SPEC.md`, and `src/SPEC.md` to keep behavior documentation aligned.

### [CORRECTION] mqtt_client disconnected publish test aligned with transport contract
`src/yaha/mqtt_client/test/broker_transport_test.cpp` expected disconnected publish as no-op, but broker transport now throws on disconnected publish. Test updated to assert runtime error for publish while keeping subscribe/unsubscribe/ping/disconnect no-op-safe; `src/yaha/mqtt_client/test/TEST_SPEC.md` synchronized.

### [CORRECTION] file_store watcher key-path test hardened against snapshot race
`src/yaha/file_store/test/file_store_test.cpp` watcher test could fail when filesystem change was observed as `created` instead of `changed` due timing between watcher snapshots and out-of-band write. Test now verifies deterministic setup, forces out-of-band mtime change, and accepts race-equivalent filesystem event (`created` or `changed`) while still requiring `source=filesystem-watch` and expected `keyPath`; `src/yaha/file_store/test/TEST_SPEC.md` synchronized.

### [MILESTONE] Full unit-only Python scripts green after follow-up fixes
Executed `python3 test/run_coverage_broker.py --unit-only` and `python3 test/run_coverage_clients.py --unit-only` to completion after fixes; both reports are OK with selected broker/client tests passing.
