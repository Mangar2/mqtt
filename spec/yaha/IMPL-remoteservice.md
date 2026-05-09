# Implementation Plan: RemoteService Client 1.0

This plan defines the implementation sequence for
[SPEC-remoteservice.md](./SPEC-remoteservice.md).

The scope is a new standalone YAHA RemoteService client with mandatory
FileStore-backed mapping load and monitor-triggered reload behavior.

## Goal

Implement one coherent RemoteService standalone client that:
- accepts HTTP GET/POST commands on configured paths
- requires token validation for both request modes
- resolves command output topics from a FileStore-loaded service/device mapping
- publishes MQTT command messages with mapped topic and configured QoS/reason
- reloads mapping on matching FileStore monitor events
- is fully deliverable as build target + deployment package component

## Scope

In scope:
- RemoteService domain component implementing `IMqttComponent`
- HTTP request adapter for GET/POST contract from spec
- FileStore HTTP integration (GET mapping payload)
- FileStore monitor topic handling for mapping reload
- standalone runtime config mapping and executable composition
- focused unit/component/integration tests
- deployment integration (`create_yaha_deployment.py`, `deploy_yaha_scp.py`)
- shipped INI template for runtime and deployment

Out of scope:
- FileStore server implementation changes
- broker runtime transport changes
- non-RemoteService YAHA components except integration touch points listed above

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
- duplicate-path handling (first wins, others ignored with `std::cerr` error)
- RemoteService component implementation (`IMqttComponent`)
- FileStore monitor payload parser helper

2. HTTP adapter module: `src/yaha/remote_service_http/`
- HTTP request parser for GET and POST
- mapping from HTTP request to domain command request struct
- token validation hook integration
- response adapter for success/error contract (`200`, `400`, `404`)

3. Runtime module: `src/yaha/remote_service_client/`
- INI runtime config loader
- mapping from `[filestore]`, `[remoteservice]`, `[mqtt]`
- runtime composition helpers for standalone process

Standalone entry point:
- `src/yaha_remoteserviceclient_main.cpp`

## Mandatory output artifacts

Implementation is only complete when all artifacts below exist and are wired.

1. Build/runtime artifacts
- executable `yaharemoteserviceclient`
- standalone entrypoint `src/yaha_remoteserviceclient_main.cpp`
- CMake target and install integration in `CMakeLists.txt`

2. Runtime config artifacts
- template `cmake/ini/remoteservice.ini`
- runtime parser support for this INI contract
- no hardcoded fallback for mapping key path

3. Deployment artifacts
- `create_yaha_deployment.py` includes component `remoteservice` with:
	- binary `yaharemoteserviceclient`
	- ini `remoteservice.ini`
	- service unit `yaharemoteserviceclient.service`
	- executable command line for systemd
- `deploy_yaha_scp.py` supports deploying/installing `remoteservice`
- deployment output contains shipped INI:
	`deployment/yaha/remoteservice/remoteservice.ini`

4. Operational quality artifacts
- meaningful startup/runtime logging
- meaningful CLI options including `--help`

## Implementation phases

## Phase 1: Contracts and config mapping

Step 1. Define RemoteService config contract
- add fields for listener, monitor prefix, FileStore endpoint and mapping key path
- make FileStore mandatory (no `fileStoreEnabled` flag)
- enforce `mappingKeyPath` required from INI (no implicit default fallback)
- reject invalid qos/port values in parser paths

Step 2. Implement runtime INI mapping
- parse `[filestore]` and `[remoteservice]` settings
- map `filestore.filename -> mappingKeyPath`
- map `filestore.topicPrefix -> monitorTopicPrefix`
- fail fast on missing required FileStore endpoint settings or mapping key path
- provide one loader for domain config and one for full runtime config

Deliverable:
- valid runtime config object for RemoteService + MQTT runtime

## Phase 2: Mapping model and FileStore lifecycle

Step 3. Implement mapping payload validator
- parse FileStore payload JSON object with `services` array
- validate each service entry (`path`, `devices`, optional `qos`, optional `reason`)
- reject invalid full payload without partial apply
- enforce duplicate-path rule:
	- first path entry kept
	- all later duplicates ignored
	- each duplicate emits error to `std::cerr`

Step 4. Implement startup mapping load
- on `run()` call FileStore GET for configured `mappingKeyPath`
- validate and atomically replace in-memory `servicesByPath`
- keep empty map on startup load failure and continue runtime

Step 5. Implement monitor-triggered reload
- subscribe to `<monitorTopicPrefix>/#`
- detect monitor payload with matching `keyPath`
- reload from FileStore and atomically swap map when valid

Deliverable:
- complete FileStore-backed mapping lifecycle with safe fallback behavior

## Phase 3: Domain command resolution and publish

Step 6. Implement command resolution API in component
- define request DTO: `path`, `deviceId`, `state`, token
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
- extract `deviceId`, `state`, required `accessToken` from query
- validate token via configured validator hook
- call component command resolution and publish API
- return `200 ok` on success
- return `400` on input/token validation failure
- return `404 Service not found` on unknown path/device

Step 9. Implement POST request adapter
- parse JSON payload with `deviceId`, `state`, required `deviceToken`
- validate token via configured validator hook
- call component command resolution and publish API
- return `200 ok` on success
- return `400` on input/token validation failure
- return `404 Service not found` on unknown path/device

Step 10. Bind dynamic service-path routing
- support exact path matching controlled by loaded mapping
- keep path matching case-sensitive and exact

Deliverable:
- HTTP contract wired to domain mapping behavior

## Phase 5: Standalone executable, CLI, and logging

Step 11. Add standalone main
- add executable main `src/yaha_remoteserviceclient_main.cpp`
- load INI config
- construct component, HTTP adapter, and MQTT runtime
- run until signal shutdown with clean close order

Step 12. Add meaningful CLI parameters
- required baseline:
	- `[config-path]` positional config file path
	- `--help`/`-h`
	- `--trace-messages`
- reject unknown flags with clear error text

Step 13. Add meaningful operational logging
- startup summary log block:
	- binary name/version
	- config path
	- MQTT host/port/clientId
	- FileStore host/port/mappingKeyPath
	- HTTP listen host/port
	- monitor topic prefix and qos
- mapping lifecycle logs:
	- startup load success/failure
	- reload trigger matched/ignored
	- reload success/failure
	- duplicate-path errors to `std::cerr`

Deliverable:
- runnable `yaharemoteserviceclient` with clear CLI and logs

## Phase 6: Build and deployment integration

Step 14. Wire CMake build/install
- add executable target `yaharemoteserviceclient` to `CMakeLists.txt`
- include main file and shared library sources
- apply existing compile/link/include pattern used by other YAHA clients
- include target in install list

Step 15. Add INI template and deployment copy rules
- add `cmake/ini/remoteservice.ini` template with required keys
- update `create_yaha_deployment.py`:
	- add component `remoteservice`
	- copy binary + INI
	- generate service file + install.sh
	- include in root install order

Step 16. Add remote deploy/install support
- ensure `deploy_yaha_scp.py --install-component remoteservice` works
- verify protected-config overwrite logic covers `remoteservice.ini` and service file

Deliverable:
- deployment package and remote installer include RemoteService end-to-end

## Phase 7: Verification

Step 17. Unit tests
- config parser success/failure and required field behavior
- mapping payload validator for valid/invalid structures
- duplicate-path first-wins behavior and `std::cerr` emission path
- command resolution for path/device mapping
- monitor payload keyPath matching logic

Step 18. Component tests with mock FileStore HTTP server
- startup load applies valid mapping
- startup load failure keeps empty map and continues
- monitor changed event reloads mapping
- invalid reload payload keeps previous map
- duplicate-path payload keeps first mapping only

Step 19. HTTP integration tests
- GET success publishes mapped MQTT command
- POST success publishes mapped MQTT command
- unknown path/device returns `404 Service not found`
- malformed POST JSON returns `400`
- missing token returns `400`
- invalid token returns `400`

Step 20. Packaging/deployment verification
- build preset produces `yaharemoteserviceclient`
- `create_yaha_deployment.py` outputs `deployment/yaha/remoteservice/*`
- deployment folder contains `remoteservice.ini`, service file, install script
- `deploy_yaha_scp.py` copies and can install `remoteservice` remotely

Deliverable:
- deterministic behavior and delivery pipeline verified against spec contract

## Acceptance criteria (100% implementation done)

1. `yaharemoteserviceclient` is buildable and runnable from INI config.
2. FileStore is mandatory and mapping key path is required from INI.
3. Startup load + monitor reload work against FileStore key path contract.
4. Duplicate service paths are rejected by rule, logged to `std::cerr`, and first path wins.
5. HTTP GET/POST require token and return status codes exactly as specified (`200`/`400`/`404`).
6. Startup/runtime logging is present and operationally useful.
7. CLI is present with `--help`, config-path, and message tracing option.
8. `cmake/ini/remoteservice.ini` exists and is used by runtime parser.
9. `create_yaha_deployment.py` includes RemoteService component packaging.
10. `deploy_yaha_scp.py` supports copying/installing RemoteService artifact set.
11. Deployment output contains shipped INI for RemoteService.
12. Tests for config, mapping lifecycle, HTTP behavior, and deployment integration pass.

## Risks and implementation notes

1. Concurrent reload and request execution
- use one synchronized map access path to avoid torn reads during reload.

2. Token validator dependency
- keep validator integration explicit and test doubles available for unit tests.

3. Large mapping payload latency
- parse-then-swap model to keep request path lock duration low.

4. Deployment drift risk
- keep component names/binary names/service names aligned across CMake, INI,
	create script, and deploy script.

## Step execution order summary

Step 1 -> Step 2 -> Step 3 -> Step 4 -> Step 5 -> Step 6 -> Step 7 -> Step 8 -> Step 9 -> Step 10 -> Step 11 -> Step 12 -> Step 13 -> Step 14 -> Step 15 -> Step 16 -> Step 17 -> Step 18 -> Step 19 -> Step 20