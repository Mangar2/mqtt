# Implementation Plan: MessageStore

This plan covers the implementation of the MessageStore as a standalone YAHA program.
It establishes the shared foundations (Message, IMqttComponent, MQTT session) that all subsequent YAHA MQTT services will reuse.

Specs referenced throughout:
- [SPEC-message.md](./SPEC-message.md)
- [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md)
- [SPEC-messagestore.md](./SPEC-messagestore.md)

---

## Phase 1 — Shared foundations

These steps produce reusable building blocks. Every future YAHA MQTT service depends on them.

### Step 1 — Message type

Implement the `Message` type as specified in [SPEC-message.md](./SPEC-message.md).

What is produced:
- The `Message` struct/class with all fields (`topic`, `value`, `reason`, `qos`, `retain`).
- The `ReasonEntry` type.
- The `isOn` convenience method.
- Validation at system boundaries.
- Clone semantics.

When done: any YAHA code can create, copy, and validate a Message. No MQTT, no broker, no component logic yet.

---

### Step 2 — IMqttComponent interface

Implement the `IMqttComponent` pure virtual interface as specified in [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md).

What is produced:
- `getSubscriptions()` — returns the topic→QoS map.
- `handleMessage(Message)` — receives an incoming message.
- `setPublishCallback(...)` — optional; injects the outgoing publish function.
- Virtual destructor.

When done: any component can be written against this interface without knowing about MQTT transport.

---

### Step 2.5 — MQTT-IMqttComponent wiring client foundation

Create the reusable MQTT client foundation that accepts an `IMqttComponent` and can wire any
future YAHA MQTT domain component to broker transport logic.

What is produced:
- A reusable client/session layer that accepts an `IMqttComponent` instance and drives it.
- Transport-side implementation based on existing code under `src/client` (connection negotiation,
     packet encode/decode, keep-alive, subscribe/publish flow).
- Component wiring contract: `getSubscriptions()`, inbound dispatch to `handleMessage()`, and
     outgoing publish integration via `setPublishCallback(...)`.

When done: every future YAHA MQTT domain client can reuse the same MQTT client code unchanged and
only provide a new `IMqttComponent` implementation.

---

### Step 3 — MQTT session (YahaMqttClient)

Implement the MQTT client that drives any `IMqttComponent`. This is the transport half of the composition pattern described in [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md) (section: Composition in main).

What is produced:
- Connect to yahabroker and maintain the connection.
- Reconnect automatically on connection loss; re-subscribe after every (re)connect by calling `getSubscriptions()` on the wired component.
- Forward incoming messages that match subscriptions to `handleMessage()`.
- Expose a `publish(Message)` method that the component can call via the injected callback.
- Keep-alive / ping loop.
- Graceful shutdown.

When done: a fully functional MQTT session that can drive any `IMqttComponent` without knowing what the component does. This client is reused unmodified for every future YAHA MQTT service.

---

## Phase 2 — MessageStore component

### Step 4 — Message tree

Implement the internal message tree as specified in [SPEC-messagestore.md](./SPEC-messagestore.md) (section: Data model).

What is produced:
- Tree keyed by topic path segments.
- `addData(Message)` — inserts or updates a node; moves current value to history.
- `getSection(topic, depth, includeHistory, includeReason)` — returns a flat array of nodes under a topic prefix.
- `getNodes(snapshot)` — returns only nodes that differ from the provided snapshot (change-detection).
- `cleanup(daysWithoutUpdate)` — removes stale nodes.
- History bounded by configurable max length with hysteresis.
- History stored in compressed form; decompressed on retrieval.

When done: a self-contained, independently testable data structure with no MQTT or HTTP dependency.

---

### Step 5 — MessageStore persistence

Implement periodic serialisation and startup restore as specified in [SPEC-messagestore.md](./SPEC-messagestore.md) (section: Persistence).

What is produced:
- Serialise the tree to a file at a configured interval.
- Restore from the most recent valid file on startup.
- Retain a configurable number of old files; delete older ones.
- Silent handling of missing or corrupt files (start empty).

When done: the tree survives process restarts.

---

### Step 6 — MessageStore component logic

Implement the `MessageStore` class that implements `IMqttComponent` as specified in [SPEC-messagestore.md](./SPEC-messagestore.md) (sections: Behavior, Subscriptions).

What is produced:
- `getSubscriptions()` — returns the configured topic→QoS map.
- `handleMessage(Message)` — dispatches to:
  - cleanup if topic is `$MONITOR/messages/cleanup`,
  - `tree.addData()` for all other topics.
- Lifecycle: `run()` starts the HTTP server and persistence loop; `close()` stops both and triggers a final persist.

When done: a complete `IMqttComponent` that records messages and manages its own HTTP server and persistence. Has no knowledge of MQTT transport.

---

### Step 7 — HTTP query interface

Implement the HTTP GET endpoint as specified in [SPEC-messagestore.md](./SPEC-messagestore.md) (section: External interfaces).

What is produced:
- HTTP server listening on the configured port (via cpp-httplib).
- `GET /store/<topic>` handler: parse path, headers (`levelamount`, `history`, `reason`), optional JSON body for change-detection mode.
- Dispatch to `tree.getSection()` or `tree.getNodes()`.
- Serialise result to JSON; return with correct content-type header.
- Return error for unknown paths.

When done: the MessageStore answers HTTP queries from dashboards and other services.

---

## Phase 3 — Composition and executable

### Step 8 — Main entry point

Compose and wire the program as specified in [SPEC-messagestore.md](./SPEC-messagestore.md) (section: Standalone program structure).

What is produced:
- Load configuration from file.
- Construct `MessageStore` with its configuration.
- Construct `YahaMqttClient` with its configuration.
- Wire: inject `MessageStore` as the `IMqttComponent` into `YahaMqttClient`; inject the client's `publish` function into `MessageStore` via `setPublishCallback`.
- Start: call `YahaMqttClient.run()`.
- Handle shutdown signal: call `close()` on both in order.
- CMake target for the `yahamsgstoreclient` executable; link cpp-httplib include path.

When done: a deployable binary that connects to yahabroker, records all messages, and serves HTTP queries.

---

## Phase 4 — Verification

### Step 9 — Unit tests for message tree and Message type

- Test `addData`, `getSection`, `getNodes`, `cleanup` in isolation.
- Test history bounds and compression behaviour.
- Test `Message` validation, `isOn`, clone.

### Step 10 — Integration test

- Start yahabroker and the MessageStore binary.
- Publish a set of messages; query over HTTP and verify the response matches what was published.
- Publish `$MONITOR/messages/cleanup`; verify stale nodes are removed.
- Restart MessageStore; verify persisted state is restored and HTTP queries return correct data.

---

## Dependency order summary

```
Step 1 (Message)
  └─ Step 2 (IMqttComponent)
       └─ Step 2.5 (MQTT-IMqttComponent wiring foundation)
       └─ Step 3 (YahaMqttClient)        ← reused by all future services
  └─ Step 4 (Message tree)
       └─ Step 5 (Persistence)
       └─ Step 6 (MessageStore component)
            └─ Step 7 (HTTP interface)
                 └─ Step 8 (main / executable)
                      └─ Step 9 (unit tests)
                      └─ Step 10 (integration test)
```

Steps 1–3 are the shared foundation. They must be complete before any other YAHA MQTT service is built.
