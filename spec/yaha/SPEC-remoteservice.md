# RemoteService

## Purpose

RemoteService is a YAHA component that exposes controlled HTTP endpoints and
translates approved external requests into MQTT command publishes.

Unlike legacy behavior with static local mapping, YAHA RemoteService loads the
input-to-output mapping from FileStore and reloads it on matching FileStore
monitoring events.

## Normative deviation from original behavior

This specification intentionally and explicitly deviates from the original
RemoteService persistence behavior.

Required deviation:
- mapping/settings source is FileStore, not a local configuration file.
- FileStore usage is mandatory in YAHA RemoteService.
- this is a deliberate product requirement in YAHA scope and is not an open
  design question.
- implementations and reviews must treat this deviation as fixed input.

## Role in the system

RemoteService is a domain component behind the YAHA MQTT runtime boundary
defined by [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md).

It consumes:
- HTTP `GET` and `POST` requests to configured service paths.
- FileStore startup snapshot for service mapping.
- FileStore monitoring events for mapping reload (`$MONITOR/FileStore/#` by default).

It produces:
- MQTT command publishes based on configured service/device mapping.
- HTTP success or error responses for caller requests.

## Standalone program structure

RemoteService standalone executable contains four composed parts:
1. Generic YAHA MQTT client runtime.
2. RemoteService domain component (`IMqttComponent`) with FileStore-backed
   mapping lifecycle.
3. HTTP server adapter that receives request input and delegates to component
   command handling.
4. Runtime config mapper from INI into domain and MQTT runtime config.

Main wiring model:
1. Load and sanitize runtime config.
2. Construct RemoteService component with config.
3. Construct HTTP server and bind request handlers.
4. Inject MQTT publish callback into component.
5. Call component `run()` to load mapping from FileStore.
6. Start MQTT runtime and HTTP listener.

Architecture rule:
- HTTP transport concerns stay in HTTP adapter.
- MQTT transport concerns stay in generic runtime.
- RemoteService domain logic stays transport-agnostic except explicit request
  input contract and outbound publish intent.

## Subscriptions

`getSubscriptions()` returns map `topicPattern -> qos`.

Mandatory subscriptions:
1. FileStore monitor subscription: `<monitorTopicPrefix>/#` with configured
   `subscribeQos`.

No command-topic subscription is required for RemoteService request forwarding.

## Published messages

RemoteService publishes one MQTT message per accepted HTTP command request.

Command publish contract:
- topic: resolved from mapping `services[path].devices[deviceId]`
- payload: request state value (`state`)
- qos: service-specific `qos` or default `1`
- retain: `false`
- reason: service-specific `reason` or default `remote command`

Monitoring or management publish:
- none required by legacy behavior.

## External interfaces

Public component interface:
- `run()`
- `close()`
- `getSubscriptions() -> map`
- `handleMessage(message)`
- `setPublishCallback(callback)`

Remote invocation interface:

1. HTTP `POST <servicePath>`
- payload must be JSON object with:
  - `deviceId` (required)
  - `state` (required)
  - `deviceToken` (required, must pass token validation)
- success response:
  - status `200`
  - content type `text/plain; charset=UTF-8`
  - payload `ok`

2. HTTP `GET <servicePath>`
- query parameters:
  - `deviceId` (required)
  - `state` (required)
  - `accessToken` (required, must pass token validation)
- success response:
  - status `200`
  - content type `text/plain; charset=UTF-8`
  - payload `ok`

Error response compatibility:
- input validation and token validation failures return status `400`.
- unknown service path or unknown device id returns status `404` and payload
  `Service not found`.

Persistence interface dependency:
- FileStore HTTP API from [SPEC-filestore.md](./SPEC-filestore.md)
- GET `<mappingKeyPath>` for startup and monitor-triggered reload

## Data model

## Mapping payload in FileStore

Root object shape:
```json
{
  "services": [
    {
      "path": "/service/device",
      "reason": "optional reason",
      "qos": 1,
      "devices": {
        "kitchen-light": "house/kitchen/light/set"
      }
    }
  ]
}
```

Service entry rules:
- `path`: required string, exact-match key for HTTP request path.
- `devices`: required object, maps request `deviceId` to MQTT topic.
- `qos`: optional, allowed values `0 | 1 | 2`.
- `reason`: optional string.

Validation rule:
- mapping payload is accepted only if full structure validates.
- invalid payload must not replace current in-memory mapping.

Duplicate path rule:
- duplicate `services[].path` entries are forbidden by configuration contract.
- runtime compatibility handling is deterministic: first occurrence is kept,
  all later duplicates are ignored.
- each ignored duplicate must emit an error message to `std::cerr`.

## In-memory state

- `servicesByPath`: map `<path> -> serviceEntry`
- `subscribeQos`: qos used for FileStore monitor subscription
- FileStore settings (`host`, `port`, `mappingKeyPath`)
- monitor settings (`monitorTopicPrefix`)
- request listener settings (`listenHost`, `listenPort`)

## Behavior

## Startup

On `run()`:
1. Request GET from FileStore `mappingKeyPath`.
2. If GET succeeds and payload validates, replace in-memory mapping.
3. If load fails, keep empty map and continue runtime.

Startup fallback behavior:
- no valid mapping means all HTTP requests are rejected by compatibility
  error response (`404 Service not found`).

## HTTP request handling

For each inbound HTTP request:
1. Resolve service by exact request path match.
2. Extract input fields:
  - POST JSON: `deviceId`, `state`, required `deviceToken`
  - GET query: `deviceId`, `state`, required `accessToken`
3. Validate required token for selected request mode.
4. Resolve output topic by `service.devices[deviceId]`.
5. Build outbound MQTT message using resolved topic and input `state`.
6. Publish through callback.
7. Return success response (`200`, `ok`).

Error behavior:
- malformed input payload, missing required input fields, and failed token
  validation are treated as bad request and return `400`.
- missing service path and unknown device id return `404`.
- publish callback errors return `404`.

## Message handling

For each inbound MQTT message:

1. If topic matches monitor prefix and indicates FileStore change for
   `mappingKeyPath`:
   - reload mapping from FileStore GET
   - if reload succeeds, replace in-memory map atomically

2. Else:
   - no action

Reload behavior:
- reload is whole-map replacement, not incremental patch merge.
- failed reload keeps previous valid map unchanged.

## Persistence

RemoteService mapping source of truth is FileStore service.

Mandatory persistence rules:
- load mapping from FileStore on startup.
- reload mapping from FileStore on matching monitor event.
- no local mapping file read/write in YAHA runtime path.
- no RemoteService write-back to FileStore in this version.
- any original local-config-file mapping source is intentionally disabled in
  YAHA RemoteService.
- startup must fail fast if FileStore endpoint settings or `mappingKeyPath`
  are missing in runtime config.

## Configuration

Required/optional runtime config contract:

Component settings:
- `listenHost`: string (default `0.0.0.0`)
- `listenPort`: integer (default `9123`)
- `subscribeQos`: `0 | 1 | 2` (default `1`)
- `monitorTopicPrefix`: string (default `$MONITOR/FileStore`)

FileStore settings:
- `fileStoreHost`: string (default `127.0.0.1`)
- `fileStorePort`: integer (default `8210`)
- `mappingKeyPath`: string (required, no implicit default)

INI mapping convention aligned with ValueService client:
- `[filestore] filename` is a compatibility alias mapped to `mappingKeyPath`
  (FileStore key path only, never a local filesystem path).
- `[filestore] topicPrefix` is mapped to `monitorTopicPrefix`.

INI requirement for startup path:
- `mappingKeyPath` must be configured in `.ini` via `[filestore] filename`.
- startup must not continue with a hardcoded fallback path.

## Error handling

- Invalid mapping payload:
  - reject payload
  - keep previous in-memory mapping
  - log validation context
- FileStore GET failure:
  - startup: continue with empty map
  - monitor reload: keep existing mapping
- Duplicate service path in loaded mapping:
  - report error to `std::cerr`
  - keep first occurrence, ignore all following duplicates
  - continue runtime with deduplicated in-memory map
- HTTP input parse failure:
  - return `400`
- Missing required request fields:
  - return `400`
- Token validation failure:
  - return `400`
- Unknown service path or device id:
  - return `404 Service not found`
- MQTT publish callback failure:
  - return `404 Service not found`
  - runtime remains alive

## Architectural notes

- RemoteService keeps legacy request/response compatibility for callers while
  changing mapping source from local static config to FileStore lifecycle.
- RemoteService does not subscribe/publish `$SYS` topics.
- FileStore remains infrastructure dependency, not embedded persistence logic.
