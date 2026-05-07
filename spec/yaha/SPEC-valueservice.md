# ValueService

## Purpose

ValueService is a YAHA component that keeps a runtime map of topic-keyed values and
serves as a retained-value publisher for those topics.

It provides two core behaviors:
- consume `<topic>/set` commands and publish retained state updates on `<topic>`
- load and persist the complete value map through FileStore service APIs

## Role in the system

ValueService is a domain component behind the YAHA MQTT runtime boundary defined by
[SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md).

It consumes:
- MQTT set commands (`<topic>/set`)
- FileStore startup snapshot (HTTP GET)
- FileStore monitoring events for reload (`$MONITOR/FileStore/#` by default)

It produces:
- retained MQTT state messages (`<topic>`)
- FileStore write-back updates (HTTP POST full value map)

## Standalone program structure

ValueService standalone executable contains two composed parts:
1. Generic YAHA MQTT client runtime.
2. ValueService domain component.

Main wiring model:
1. Load and sanitize runtime config.
2. Construct ValueService component with config.
3. On `run()`, load full value map from FileStore key path.
4. Ask component for subscriptions.
5. Start MQTT runtime loop.
6. Route inbound MQTT messages to `handleMessage`.
7. Publish all returned outbound messages.

Architecture rule:
- MQTT transport concerns stay in runtime.
- ValueService stays transport-agnostic and exposes behavior through `IMqttComponent`.

## Subscriptions

`getSubscriptions()` returns map `topicPattern -> qos`.

Mandatory subscriptions:
1. One subscription per known value key: `<key>/set` with configured `subscribeQos`.
2. FileStore monitor subscription: `<monitorTopicPrefix>/#` with `subscribeQos`.

Dynamic subscription rule:
- Whenever value keys change after reload or update, subscriptions for key `/set`
  topics are recomputed from current in-memory keys.

## Published messages

ValueService publishes two categories:

1. Value state publishes
- topic: `<key>`
- payload: stored value for key
- qos: configured `subscribeQos`
- retain: `true`
- trigger:
  - startup replay in `run()` for all loaded keys
  - each accepted inbound `<key>/set` message

2. Optional monitor/ack publishes
- none required by legacy ValueService behavior
- component may rely on runtime logging for diagnostics

## External interfaces

Public component interface:
- `run()`
- `close()`
- `getSubscriptions() -> map`
- `handleMessage(message) -> Message[]`
- `setPublishCallback(callback)`

Persistence interface dependency:
- FileStore HTTP API from [SPEC-filestore.md](./SPEC-filestore.md)
- GET `<valuesKeyPath>` for startup and monitor-triggered reload
- POST `<valuesKeyPath>` with full value map after each accepted set command

## Data model

## Value map

Root object map:
- key: MQTT topic string (without `/set` suffix)
- value: string or integer

Example shape:
```json
{
  "house/livingroom/temp/target": 21,
  "house/presence/state": "home"
}
```

Validation rule:
- only string and integer values are accepted in loaded or updated maps.

## In-memory state

- `values`: full key/value map
- `subscribeQos`: qos used for subscriptions and outbound state publishes
- file-store settings (`enabled`, `host`, `port`, `valuesKeyPath`)
- monitor settings (`monitorTopicPrefix`)

## Behavior

## Startup

On `run()`:
1. If `fileStoreEnabled=true`, request GET from FileStore `valuesKeyPath`.
2. If GET succeeds and payload validates, replace in-memory value map.
3. Publish one retained state message for each key/value pair.

Startup fallback behavior:
- If FileStore load fails or payload is invalid, component starts with empty map
  and logs error context.

## Message handling

For each inbound message:

1. If topic matches monitor prefix and indicates FileStore change for
   `valuesKeyPath`:
   - reload full map from FileStore GET
   - if reload succeeds, replace full map and publish retained replay for all keys

2. Else if topic ends with `/set`:
   - derive key by removing `/set`
   - set `values[key] = incoming.value`
   - build one state message on `<key>` with same payload, retain=true, qos=subscribeQos
   - persist full map to FileStore via POST
   - return state message for runtime publish

3. Else:
   - no action

Persistence write policy:
- Write-back is whole-map (full snapshot), not per-key delta files.
- FileStore write errors must not block outbound MQTT state publish.

## Persistence

Persistence source of truth for ValueService is FileStore service.

Mandatory persistence rules:
- Load from FileStore on startup.
- Persist full map to FileStore after each accepted `/set` update.
- Local file read/write is not used in YAHA ValueService runtime.

Monitoring-based reload:
- Subscribe to FileStore monitor topic prefix (default `$MONITOR/FileStore`).
- On matching change event for configured key path, reload current map from FileStore.

## Configuration

Required/optional config contract for ValueService client runtime:

Component settings:
- `subscribeQos`: `0 | 1 | 2` (default `1`)
- `monitorTopicPrefix`: string (default `$MONITOR/FileStore`)

FileStore settings:
- `fileStoreEnabled`: bool (default `true`)
- `fileStoreHost`: string (default `127.0.0.1`)
- `fileStorePort`: integer (default `8210`)
- `valuesKeyPath`: string (default `/valueservice/values`)

Compatibility note:
- Legacy `valuesFileName` remains configuration-compatible only for migration, but
  must not be used as active persistence source in YAHA ValueService runtime.

## Error handling

- Invalid loaded map payload (wrong shape or unsupported value type):
  - reject payload
  - keep previous in-memory map
  - log validation errors
- FileStore GET failure:
  - startup: continue with empty map
  - monitor reload: keep existing in-memory map
- FileStore POST failure after `/set`:
  - still publish outbound retained MQTT state update
  - log persistence failure
- Unsupported inbound payload/value type:
  - ignore update
  - return no outbound message

## Architectural notes

- ValueService persistence strategy is intentionally aligned with Automation client:
  full-tree GET at startup and full-tree POST on managed updates.
- ValueService does not subscribe/publish `$SYS` topics.
- FileStore service remains an infrastructure dependency, not embedded logic.

## Open questions

- Should ValueService publish a dedicated management acknowledgment topic for
  persistence failures, or keep current silent-fail-with-log behavior?
- Should monitor-triggered reload publish only changed keys or always full replay?
- Should bool values be accepted in addition to string/integer for future parity
  with broader YAHA value usage?
