# remote_service_client — YAHA RemoteService Runtime Types, INI Mapping, and Standalone Composition

## Purpose

Defines RemoteService standalone runtime config data types, RemoteService
specific INI mapping behavior, and standalone process composition entrypoint.

## Public API

### Struct `RemoteServiceClientRuntimeConfig`

| Field | Type | Notes |
|------|------|-------|
| `remoteServiceConfig` | `RemoteServiceConfig` | RemoteService domain settings |
| `mqttConfig` | `YahaMqttClient::Config` | Generic MQTT runtime settings |

### Config mapping helpers

| Function | Signature | Notes |
|---------|-----------|-------|
| `tryLoadRemoteServiceConfigFromIni` | `(const IniDocument&, RemoteServiceConfig&, std::string&) -> bool` | Maps `[filestore]` and `[remoteservice]` into domain config and validates required fields |
| `tryLoadRemoteServiceClientRuntimeConfigFromIni` | `(const IniDocument&, RemoteServiceClientRuntimeConfig&, std::string&) -> bool` | Maps full standalone runtime config including MQTT section |

## Configuration format

Supported INI sections:

- `[mqtt]`
  - `host`, `port`, `clientId`, `reconnectDelayMs`, `keepAliveIntervalMs`, `loopSleepMs`, `logReason`
- `[filestore]`
  - `host` (required), `port` (required), `filename` (required), `topicPrefix`
- `[remoteservice]`
  - `listenHost`, `listenPort`, `subscribeQoS`

Validation rules:

- `filestore.host` must be present and non-empty.
- `filestore.port` must be present and in range `1..65535`.
- `filestore.filename` must be present and non-empty.
- `remoteservice.listenPort` must be in range `1..65535` when set.
- `remoteservice.subscribeQoS` must be in range `0..2` when set.

Mapping rules:

- `filestore.filename -> RemoteServiceConfig.mappingKeyPath`
- `filestore.topicPrefix -> RemoteServiceConfig.monitorTopicPrefix`

## Standalone composition behavior

`src/yaha_remoteserviceclient_main.cpp` composes runtime directly:

- parse CLI args (`[config-path]`, `--trace-messages`, `--help`)
- load INI config with `IniDocument`
- map full runtime config with `tryLoadRemoteServiceClientRuntimeConfigFromIni`
- construct `RemoteServiceComponent`
- construct `RemoteServiceHttpAdapter`
- construct `YahaMqttClient` with `makeBrokerTransport()`
- start component and MQTT client lifecycle
- start HTTP listener for dynamic path routing
- run until signal and perform clean shutdown

## Deployment integration

Phase 6 deployment artifacts for RemoteService are wired through packaging scripts:

- INI template: `cmake/ini/remoteservice.ini`
- deployment component name: `remoteservice`
- packaged binary: `yaharemoteserviceclient`
- packaged service unit: `remotesvc.service`

`cmake/create_yaha_deployment.py` includes `remoteservice` in component packaging
and root install order. `cmake/deploy_yaha_scp.py` supports remote component install
via `--install-component remoteservice` and treats RemoteService `.ini` and `.service`
files as protected config overwrite prompts.

## Files

| File | Role |
|------|------|
| `remote_service_client_app.h` | Runtime config declarations and loader signatures |
| `remote_service_client_app.cpp` | Runtime config parsing and validation implementation |

Related runtime entrypoint:

- `src/yaha_remoteserviceclient_main.cpp`