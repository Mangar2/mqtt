# FileStore

## Purpose

FileStore is a small HTTP key/value persistence service. It stores one value per key path and returns stored values by the same key path.

The component is used as persistent backend for rule trees (for example Automation rules) and can also be used for other small JSON or text payloads.

## Role in the system

FileStore is an infrastructure service in YAHA.

Extended YAHA target behavior:
- FileStore is also an MQTT-enabled component and is composed with the generic MQTT runtime via `IMqttComponent`.
- On detected file changes, FileStore emits monitoring events to broker topics under `$MONITOR/FileStore/...`.

It consumes:
- HTTP requests (`POST`, `GET`, `OPTIONS`)
- local filesystem access
- file change events from watched persistence directory

It produces:
- HTTP responses
- persisted files in configured directory
- MQTT monitoring messages for file changes

Typical integration in legacy code:
- Automation reads rules from one FileStore key path on startup.
- Automation writes full rule tree back to same FileStore key path after rule changes.

## Standalone program structure

FileStore is a standalone program composed from:
1. `HttpServer` for request handling.
2. `Persist` backend for filesystem persistence.
3. FileStore domain component implementing `IMqttComponent`.
4. generic YAHA MQTT client runtime.
5. configuration sanitizer/validator.

Entry point behavior:
1. Read config (`yahakvstore.json`).
2. Construct `KeyValueStore(configuration)`.
3. Create MQTT runtime.
4. Wire publish callback into FileStore component.
5. Start runtime orchestration (`run`/`close`) for both MQTT and HTTP lifecycle.

Lifecycle:
- `run()` registers handlers for `POST`, `GET`, `OPTIONS`, starts HTTP listening, and starts file watcher loop.
- shutdown hook closes server and exits process.
- `close()` stops watcher and server.

## Subscriptions

Default: none.

`getSubscriptions()` returns an empty mapping unless a future control channel is explicitly enabled.

## Published messages

FileStore publishes monitoring messages when file content in the configured directory changes.

Topic namespace:
- `$MONITOR/FileStore/<eventType>`

Mandatory event types:
- `$MONITOR/FileStore/changed`
- `$MONITOR/FileStore/created`
- `$MONITOR/FileStore/deleted`
- `$MONITOR/FileStore/error`

Payload contract (JSON object):
- `keyPath`: logical key path if resolvable, else `null`
- `filename`: mapped filename in persistence directory
- `directory`: configured directory
- `changeType`: one of `changed | created | deleted | error`
- `timestamp`: unix epoch in milliseconds
- `source`: one of `http-post | filesystem-watch`
- `details`: optional object for error or watcher backend details

QoS and retain defaults:
- QoS: `1`
- retain: `false`

## External interfaces

FileStore exposes HTTP endpoints by method over arbitrary request path keys.

### HTTP OPTIONS `<keyPath>`

Purpose:
- CORS preflight support.

Response:
- Status: `200`
- Headers:
  - `Access-Control-Allow-Origin: *`
  - `Access-Control-Allow-Methods: POST, GET, OPTIONS`
  - `Access-Control-Allow-Headers: content-type`
- Body: empty string

### HTTP POST `<keyPath>`

Purpose:
- Store payload under `keyPath`.

Request:
- Path is the storage key.
- Max key length: `100` characters.
- Payload handling:
  - If header `content-type` equals `application/json` (case-insensitive exact match), payload is parsed as JSON.
  - Otherwise payload is treated as raw text.

Behavior:
1. Validate key length.
2. Parse payload depending on content type.
3. Persist parsed/raw value.

Success response:
- Status: `200`
- Headers:
  - `Access-Control-Allow-Origin: *`
- Body: empty string

Error responses:
- Key too long:
  - Status: `400`
  - Body: `Error: Key too long, a maximum of <maxKeyLength> characters are supported`
- JSON parse error:
  - Status: `400`
  - Body: `Error: Invalid JSON payload`
- Persist/internal runtime error:
  - Status: `500`
  - Body: `Error: Failed to persist key (<reason>)`

### HTTP GET `<keyPath>`

Purpose:
- Read stored payload by `keyPath`.

Request:
- Path is the storage key.
- Max key length: `100` characters.

Behavior:
1. Validate key length.
2. Read stored value.
3. Return JSON-serialized payload.

Success response:
- Status: `200`
- Headers:
  - `Access-Control-Allow-Origin: *`
  - `Content-Type: Application/Json`
- Body: `JSON.stringify(storedPayload)`

Error responses:
- Key too long:
  - Status: `400`
  - Body: `Error: Key too long, a maximum of <maxKeyLength> characters are supported`
- Key not found:
  - Status: `404`
  - Body: `Error: Key not found`
- Read/internal error:
  - Status: `500`
  - Body: `Error: Failed to read key (<reason>)`

## Data model

## Key mapping

External key (`keyPath`) is not used directly as filename.

Filename mapping contract:
- For each character in `keyPath`, append its decimal character code to filename string.
- Final filename is concatenated numeric string.

Example mapping:
- `"/a"` -> `"4797"`

## Stored value type

Stored value is `any`:
- parsed JSON value for JSON POST requests,
- raw text for non-JSON POST requests.

GET always returns JSON-serialized representation of stored value.

## Internal state

- sanitized configuration
- HTTP server instance
- persist backend instance

## Behavior

## Start/stop behavior

On construction:
- sanitize and validate configuration
- initialize HTTP server on configured port
- initialize persist backend with configured `keepFiles`

On `run()`:
- register method handlers (`POST`, `GET`, `OPTIONS`)
- start HTTP server listening
- initialize file watcher on configured directory
- publish `$MONITOR/FileStore/created` and `$MONITOR/FileStore/changed` events for watcher-detected create/change updates
- publish `$MONITOR/FileStore/deleted` events for watcher-detected removals
- publish `$MONITOR/FileStore/error` on watcher/runtime errors
- register process shutdown hook to close server and exit process

On `close()`:
- stop file watcher
- close HTTP server

## MQTT monitoring behavior

Monitoring trigger sources:
1. HTTP writes (`POST`) after successful persistence.
2. Filesystem watcher events for out-of-band modifications.

Publish rules:
1. On successful `POST` write, publish one change event with `source = http-post`.
2. On watcher create/change/delete events, publish corresponding monitoring event with `source = filesystem-watch`.
3. Do not publish monitoring message for failed `POST` or failed read.
4. Event publish failures must not break HTTP request handling; failures are logged and runtime continues.

Deduplication requirement:
- If one logical update is observed both from direct write path and watcher path, implementation may publish one or two events.
- If deduplication is implemented, it must be time-window based and deterministic.

## Port behavior

`port` getter returns:
- actual bound port if server already has address
- `undefined` before listen

## Persistence behavior

Write path:
- `writeFile(keyPath, value)` resolves filename by key mapping.
- Persist backend stores object into configured directory.

Read path:
- `readFile(keyPath)` resolves filename by key mapping.
- Persist backend reads payload from configured directory.

## Persistence

Filesystem persistence is delegated to persist backend with:
- directory from configuration (`directory`)
- filename from key mapping function
- file retention count from `keepFiles`

Retention contract exposed by FileStore:
- `keepFiles` is passed through to persist backend unchanged.
- Legacy default is `2`.

## Configuration

Schema-derived configuration contract:

Required:
- `port`: string
- `directory`: string

Optional:
- `keepFiles`: integer (default `2`)
- `monitoring`: object
  - `enabled`: boolean (default `true`)
  - `topicPrefix`: string (default `$MONITOR/FileStore`)
  - `qos`: one of `0 | 1 | 2` (default `1`)
  - `retain`: boolean (default `false`)

Validation behavior:
- Additional properties are rejected.
- Non-object root config causes logged error and process exit with status `1`.

MQTT runtime/broker configuration remains in generic MQTT client configuration and is not redefined in this component spec.

## Error handling

- Invalid JSON payload in JSON POST path returns `400 Error: Invalid JSON payload`.
- Missing key on GET returns `404 Error: Key not found`.
- Filesystem/runtime failures on POST return `500 Error: Failed to persist key (<reason>)`.
- Filesystem/runtime failures on GET return `500 Error: Failed to read key (<reason>)`.
- Overlength keys return `400 Error: Key too long, a maximum of <maxKeyLength> characters are supported`.
- Watcher errors trigger `$MONITOR/FileStore/error` publish attempt and local error logging.
- MQTT publish errors for monitoring messages are logged; FileStore HTTP service remains available.

## Architectural notes

- FileStore now has dual interfaces: HTTP key/value API and outbound MQTT monitoring.
- MQTT coupling follows interface boundary from [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md): domain component is broker-policy-agnostic; generic runtime owns reconnect/session policy.
- Key-to-filename mapping is deterministic and reversible only by scanning known keys externally; filesystem filenames do not expose original key path text.
- Content type matching for JSON is strict by exact token `application/json` after lowercasing; values like `application/json; charset=utf-8` are treated as non-JSON in legacy behavior.
- CORS headers are always open (`*`) for GET/POST success and OPTIONS.

## Open questions

- Should JSON content-type parsing accept media type parameters (`; charset=utf-8`) for compatibility with common HTTP clients?
- Should response `Content-Type` casing be normalized (`application/json`) or remain legacy-compatible (`Application/Json`)?
- Should authentication/authorization be added for production deployment? Legacy behavior has none.
- Should FileStore publish only watcher-based events, or both watcher and successful HTTP-write events permanently?
- Should `$MONITOR/FileStore/...` be fixed or project-configurable per deployment profile?
