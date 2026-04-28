# MessageStore

## Purpose

Receives every MQTT message that passes through the home automation broker and keeps a current, queryable snapshot of the entire system state. Clients (dashboards, automation rules, other services) query the MessageStore over HTTP to read the current value, history, and reason of any topic without needing to subscribe to MQTT themselves.

## Role in the system

The MessageStore is the single source of truth for "what is the current state of the home automation system". It is a passive recorder: it never changes device state, never publishes commands. Its MQTT subscriptions are read-only. Its HTTP interface is read-only. It is the backend for any UI or service that needs to display or reason about current state.

## Standalone program structure

The MessageStore is a standalone program. Its entry point composes:
- An **MQTT client** (connected to the broker), responsible for the transport layer.
- The **MessageStore component**, responsible for storage and HTTP serving.

The MQTT client knows the component only through the `IMqttComponent` interface (see [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md)). The MessageStore component knows nothing about the MQTT client. Both are wired together in `main`.

The MessageStore also runs an **HTTP server** as a second interface. The HTTP server is started and managed by the MessageStore component itself; it is not part of the MQTT client.

## Subscriptions

The MessageStore subscribes to all topics it should record. The subscription set is provided through configuration rather than hardcoded. Typically this is a wildcard subscription (`#`) to capture all messages, but it can be scoped.

| Topic pattern | QoS | Purpose                  |
|---------------|-----|--------------------------|
| configurable  | configurable | All topics to record |

Special topic:

| Topic                      | Meaning                                                      |
|----------------------------|--------------------------------------------------------------|
| `$SYS/messages/cleanup`    | Triggers a cleanup of stale nodes. Payload is the number of days without update after which a node is considered stale. |

## Published messages

The MessageStore does not publish any messages.

## External interfaces

### HTTP GET `/store/<topic>`

Returns the current state of one or more nodes from the message tree.

**Path parameter:** `<topic>` — the topic prefix to query. An empty or root topic returns from the root.

**Request headers:**

| Header        | Type    | Default | Meaning                                                                   |
|---------------|---------|---------|---------------------------------------------------------------------------|
| `levelamount` | integer | 1       | How many levels of child topics to include in the response                |
| `history`     | string  | false   | Include message history in the response (`"true"` / `"false"`)            |
| `reason`      | string  | true    | Include reason information in the response (`"true"` / `"false"`)         |

**Request body (optional):** a JSON array of snapshot nodes `[{topic, value, reason}, ...]`. When provided, only nodes whose current state differs from the snapshot are returned (change-detection mode). When omitted, all nodes under the requested topic path up to `levelamount` depth are returned.

**Response:** `200 OK`, `Content-Type: application/json`. Body is a JSON array of message nodes. Each node contains at minimum `topic` and `value`; `time`, `reason`, and `history` are included based on headers.

**Error:** any path that is not the configured store path returns an error. HTTP status code for errors is implementation-defined.

## Data model

The MessageStore delegates all storage to a MessageTree (see [SPEC-messagetree.md](./SPEC-messagetree.md)). The MessageStore itself holds no secondary data structure. Its own state is:

- A reference to the MessageTree instance.
- The HTTP server instance.
- A running/stopped flag.
- Configuration (subscriptions, persistence settings, server settings, tree settings).

## Behavior

### Message handling

On receiving a message via `handleMessage`:
1. If the topic is `$SYS/messages/cleanup`, the payload value is interpreted as a number of days. A cleanup is triggered on the tree with that value.
2. For all other topics, the message is passed to the tree's `addData` operation.

### HTTP query handling

On receiving a GET request on the configured path:
1. The topic is extracted from the URL path after the configured root path segment. URL-encoded spaces are decoded.
2. The `levelamount`, `history`, and `reason` header flags are parsed.
3. If the request body contains a JSON array, change-detection mode is used.
4. Otherwise, a section query is executed.
5. The result is serialised to JSON and returned.

Any parsing errors in the request body are caught and result in an empty section being returned rather than a server error.

### Lifecycle

On `run`:
- The HTTP server starts listening on the configured port.
- If persistence is configured with a non-zero interval, a periodic persist loop starts.

On `close`:
- The HTTP server stops.
- One final persist is executed before shutdown.

## Persistence

The MessageTree state is periodically serialised to disk. On startup the most recent persisted file is loaded. This allows the MessageStore to survive restarts without losing state.

| Parameter           | Meaning                                             |
|---------------------|-----------------------------------------------------|
| directory           | Directory for persistence files                     |
| filename            | Base filename; a timestamp is appended              |
| interval            | Persist interval in milliseconds; 0 disables        |
| keepFiles           | Number of old persistence files to retain           |

## Configuration

| Parameter              | Section       | Type             | Meaning                                                        |
|------------------------|---------------|------------------|----------------------------------------------------------------|
| subscriptions          | root          | object           | Map of topic pattern → QoS level                               |
| persist.directory      | persist       | string           | Directory for persistence files                                |
| persist.filename       | persist       | string           | Base filename for persistence files                            |
| persist.interval       | persist       | integer (ms)     | Persist interval; 0 = no periodic persistence                  |
| persist.keepFiles      | persist       | integer          | Number of old files to keep                                    |
| server.port            | server        | integer/string   | Port the HTTP server listens on                                |
| server.path            | server        | string           | URL path prefix for the HTTP store endpoint                    |
| tree.*                 | tree          | see MessageTree  | Passed through to MessageTree (see SPEC-messagetree.md)        |

## Error handling

- Malformed request body (invalid JSON) in HTTP requests: treated as if no body was provided; a full section query is executed.
- Unknown HTTP path: error returned to caller.
- Persistence load failure: the store starts with an empty tree and logs the failure.
- `$SYS/messages/cleanup` with a non-numeric payload: ignored.

## Architectural notes

- The HTTP server and MQTT client run concurrently. Access to the MessageTree from both the MQTT message handler and the HTTP request handler must be safe. The implementation must address this, but the mechanism is not specified here.
- The MessageStore is designed for high read frequency (dashboards polling) and moderate write frequency (broker messages). The tree structure supports O(subtree size) reads by prefix.

## Open questions

- Should the HTTP API support POST/websocket push in addition to GET polling? The legacy code is poll-only.
- Should `$SYS/messages/cleanup` be a reserved topic handled inside the MessageStore, or should cleanup be triggerable via HTTP as well?
- Is the HTTP interface sufficient, or should a gRPC or WebSocket interface be considered for the C++ reimplementation?
- What authentication/authorisation (if any) should the HTTP server enforce? The legacy code has none.
