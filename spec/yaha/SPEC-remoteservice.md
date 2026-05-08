# RemoteService

## Purpose

RemoteService is a YAHA component that exposes controlled HTTP endpoints and
translates approved external requests into MQTT command publishes.

Unlike legacy behavior with static local mapping, YAHA RemoteService loads the
input-to-output mapping from FileStore and reloads it on matching FileStore
monitoring events.

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
  - `deviceToken` (optional, currently informational)
- success response:
  - status `200`
  - content type `text/plain; charset=UTF-8`
  - payload `ok`

2. HTTP `GET <servicePath>`
- query parameters:
  - `deviceId` (required)
  - `state` (required)
  - `accessToken` (optional, currently informational)
- success response:
  - status `200`
  - content type `text/plain; charset=UTF-8`
  - payload `ok`

Error response compatibility:
- any request handling error returns status `404` and payload
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

## In-memory state

- `servicesByPath`: map `<path> -> serviceEntry`
- `subscribeQos`: qos used for FileStore monitor subscription
- FileStore settings (`enabled`, `host`, `port`, `mappingKeyPath`)
- monitor settings (`monitorTopicPrefix`)
- request listener settings (`listenHost`, `listenPort`)

## Behavior

## Startup

On `run()`:
1. If `fileStoreEnabled=true`, request GET from FileStore `mappingKeyPath`.
2. If GET succeeds and payload validates, replace in-memory mapping.
3. If load fails, keep empty map and continue runtime.

Startup fallback behavior:
- no valid mapping means all HTTP requests are rejected by compatibility
  error response (`404 Service not found`).

## HTTP request handling

For each inbound HTTP request:
1. Resolve service by exact request path match.
2. Extract input fields:
   - POST JSON: `deviceId`, `state`, optional `deviceToken`
   - GET query: `deviceId`, `state`, optional `accessToken`
3. Resolve output topic by `service.devices[deviceId]`.
4. Build outbound MQTT message using resolved topic and input `state`.
5. Publish through callback.
6. Return success response (`200`, `ok`).

Error behavior:
- missing service path, unknown device id, malformed input payload, and publish
  callback errors are treated as request failure and return `404`.

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

## Configuration

Required/optional runtime config contract:

Component settings:
- `listenHost`: string (default `0.0.0.0`)
- `listenPort`: integer (default `9123`)
- `subscribeQos`: `0 | 1 | 2` (default `1`)
- `monitorTopicPrefix`: string (default `$MONITOR/FileStore`)

FileStore settings:
- `fileStoreEnabled`: bool (default `true`)
- `fileStoreHost`: string (default `127.0.0.1`)
- `fileStorePort`: integer (default `8210`)
- `mappingKeyPath`: string (default `/remoteservice/services`)

INI mapping convention aligned with ValueService client:
- `[filestore] filename` is mapped to `mappingKeyPath`.
- `[filestore] topicPrefix` is mapped to `monitorTopicPrefix`.

## Error handling

- Invalid mapping payload:
  - reject payload
  - keep previous in-memory mapping
  - log validation context
- FileStore GET failure:
  - startup: continue with empty map
  - monitor reload: keep existing mapping
- HTTP input parse failure:
  - return `404 Service not found` (legacy-compat behavior)
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

## Open questions

- Should HTTP input validation errors use `400` in YAHA mode while keeping
  optional legacy `404` compatibility mode?
- Should `deviceToken` and `accessToken` become mandatory auth checks instead
  of informational fields?
- Should mapping reload support partial updates for very large mapping sets?