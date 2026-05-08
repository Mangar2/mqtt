# Implementation Plan: RemoteService Client 1.0

This plan defines the implementation sequence for
[SPEC-remoteservice.md](./SPEC-remoteservice.md).

The scope is a new standalone YAHA RemoteService client with FileStore-backed
mapping load and monitor-triggered reload behavior.

## Goal

Implement one coherent RemoteService standalone client that:
- accepts HTTP GET/POST commands on configured paths
- resolves command output topics from a FileStore-loaded service/device mapping
- publishes MQTT command messages with mapped topic and configured QoS/reason
- reloads mapping on matching FileStore monitor events

## Scope

In scope:
- RemoteService domain component implementing `IMqttComponent`
- HTTP request adapter for legacy-compatible GET/POST contract
- FileStore HTTP integration (GET mapping payload)
- FileStore monitor topic handling for mapping reload
- standalone runtime config mapping and executable composition
- focused unit tests and component-level integration tests

Out of scope:
- FileStore server implementation changes
- broker runtime transport changes
- authentication/authorization redesign
- non-RemoteService YAHA components

## Referenced specifications

- [SPEC-remoteservice.md](./SPEC-remoteservice.md)
- [SPEC-filestore.md](./SPEC-filestore.md)
- [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md)
- [SPEC-message.md](./SPEC-message.md)

## Target module structure

Create three YAHA modules.

1. Domain module: `src/yaha/remote_service/`
- RemoteService config type
- service mapping model and validation
- RemoteService component implementation (`IMqttComponent`)
- FileStore monitor payload parser helper

2. HTTP adapter module: `src/yaha/remote_service_http/`
- HTTP request parser for GET and POST
- mapping from HTTP request to domain command request struct
- response adapter for legacy-compatible success/error replies

3. Runtime module: `src/yaha/remote_service_client/`
- INI runtime config loader
- mapping from `[filestore]`, `[remoteservice]`, `[mqtt]`
- runtime composition helpers for standalone process

Standalone entry point:
- `src/yaha_remoteserviceclient_main.cpp`

## Implementation phases

## Phase 1: Contracts and config mapping

Step 1. Define RemoteService config contract
- add fields for listener, monitor prefix, FileStore settings and mapping key path
- set defaults from spec
- reject invalid qos/port/bool values in parser paths

Step 2. Implement runtime INI mapping
- parse `[filestore]` and `[remoteservice]` settings
- map `filestore.filename -> mappingKeyPath`
- map `filestore.topicPrefix -> monitorTopicPrefix`
- provide one loader for domain config and one for full runtime config

Deliverable:
- valid runtime config object for RemoteService + MQTT runtime

## Phase 2: Mapping model and FileStore lifecycle

Step 3. Implement mapping payload validator
- parse FileStore payload JSON object with `services` array
- validate each service entry (`path`, `devices`, optional `qos`, optional `reason`)
- reject invalid full payload without partial apply

Step 4. Implement startup mapping load
- on `run()` call FileStore GET for configured `mappingKeyPath`
- validate and atomically replace in-memory `servicesByPath`
- keep empty map on startup load failure

Step 5. Implement monitor-triggered reload
- subscribe to `<monitorTopicPrefix>/#`
- detect monitor payload with matching `keyPath`
- reload from FileStore and atomically swap map when valid

Deliverable:
- complete FileStore-backed mapping lifecycle with safe fallback behavior

## Phase 3: Domain command resolution and publish

Step 6. Implement command resolution API in component
- define request DTO: `path`, `deviceId`, `state`, optional token
- resolve service by exact `path`
- resolve topic by `service.devices[deviceId]`
- build outbound MQTT message with mapped topic/value/reason/qos

Step 7. Implement publish callback handoff
- publish resolved message through injected callback
- translate resolution or callback failures to domain error result

Deliverable:
- deterministic input-to-output mapping resolution and MQTT publish handoff

## Phase 4: HTTP adapter

Step 8. Implement GET request adapter
- extract `deviceId`, `state`, optional `accessToken` from query
- call component command resolution and publish API
- return `200 ok` on success
- return legacy-compatible `404 Service not found` on failure

Step 9. Implement POST request adapter
- parse JSON payload with `deviceId`, `state`, optional `deviceToken`
- call component command resolution and publish API
- return `200 ok` on success
- return legacy-compatible `404 Service not found` on failure

Step 10. Bind dynamic service-path routing
- support exact path matching controlled by loaded mapping
- keep path matching case-sensitive and exact for parity

Deliverable:
- HTTP compatibility interface wired to domain mapping behavior

## Phase 5: Standalone composition

Step 11. Compose runtime
- create RemoteService component from parsed config
- wire publish callback to MQTT runtime publish port
- initialize HTTP adapter with reference to component command API
- execute component `run()` before serving requests

Step 12. Add standalone main
- add executable main for RemoteService client
- load INI config
- construct component, HTTP adapter, and MQTT runtime
- run until signal shutdown with clean close order

Deliverable:
- runnable `yaharemoteserviceclient` process

## Phase 6: Verification

Step 13. Unit tests
- config parser success/failure and default paths
- mapping payload validator for valid/invalid structures
- command resolution for path/device mapping
- monitor payload keyPath matching logic

Step 14. Component tests with mock FileStore HTTP server
- startup load applies valid mapping
- startup load failure keeps empty map and continues
- monitor changed event reloads mapping
- invalid reload payload keeps previous map

Step 15. HTTP integration tests
- GET success publishes mapped MQTT command
- POST success publishes mapped MQTT command
- unknown path/device returns `404 Service not found`
- malformed POST JSON returns `404 Service not found`

Step 16. Runtime composition tests
- full runtime config mapping into domain + mqtt config
- callback wiring publishes component output through MQTT runtime

Deliverable:
- deterministic behavior verified against spec contract

## Acceptance criteria

1. Startup loads mapping from FileStore key path when enabled.
2. HTTP GET and POST requests publish exactly one mapped MQTT command on success.
3. Unknown path/device and malformed input return legacy-compatible `404` response.
4. Monitor event with matching key path reloads mapping from FileStore.
5. Invalid FileStore mapping payload never replaces valid in-memory mapping.
6. Runtime config supports defaults and field-level validation.
7. New standalone RemoteService client target is build-integrated and test-covered.

## Risks and implementation notes

1. Concurrent reload and request execution
- use one synchronized map access path to avoid torn reads during reload.

2. Large mapping payload latency
- consider optional staged parsing and swap to keep request path lock duration low.

3. Legacy compatibility ambiguity for errors
- keep strict default behavior (`404`) and make future `400` migration explicit.

4. Token handling gap
- keep token fields pass-through/informational in this version.

## Step execution order summary

Step 1 -> Step 2 -> Step 3 -> Step 4 -> Step 5 -> Step 6 -> Step 7 -> Step 8 -> Step 9 -> Step 10 -> Step 11 -> Step 12 -> Step 13 -> Step 14 -> Step 15 -> Step 16