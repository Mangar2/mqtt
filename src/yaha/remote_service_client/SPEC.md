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
  - `listenHost`, `listenPort`, `subscribeQoS`, `logIncomingMessages`, `logOutgoingMessages`, `logReason`

Validation rules:

- `filestore.host` must be present and non-empty.
- `filestore.port` must be in range `1..65535` when set.
- `filestore.filename` must be present and non-empty.
- `remoteservice.listenPort` must be in range `1..65535` when set.
- `remoteservice.subscribeQoS` must be in range `0..2` when set.

INI error handling:
- Invalid recoverable values do not abort config loading.
- For invalid recoverable values, loader writes deterministic warning logs to
  `std::cerr` and keeps the current default value.
- Missing `filestore.port` falls back to `RemoteServiceConfig` default port with warning.
- Missing `filestore.host` and missing `filestore.filename` remain non-recoverable
  and fail config loading deterministically.
- Runtime config load does not fail when MQTT sub-loader rejects one value;
  RemoteService keeps MQTT defaults and logs warning context.

Mapping rules:

- `filestore.filename -> RemoteServiceConfig.mappingKeyPath`
- `filestore.topicPrefix -> RemoteServiceConfig.monitorTopicPrefix`
- `remoteservice.logIncomingMessages -> RemoteServiceClientRuntimeConfig.logIncomingMessages`
- `remoteservice.logOutgoingMessages -> RemoteServiceClientRuntimeConfig.logOutgoingMessages`
- `remoteservice.logReason -> YahaMqttClient::Config.logReason`

## Standalone composition behavior

`src/yaha_remoteserviceclient_main.cpp` composes runtime directly:

- parse CLI args (`[config-path]`, `--trace-messages`, `--help`)
- load INI config with `IniDocument`
- map full runtime config with `tryLoadRemoteServiceClientRuntimeConfigFromIni`
- enable MQTT message trace when `--trace-messages` or any configured
  `remoteservice.logIncomingMessages/logOutgoingMessages` is true
- construct `RemoteServiceComponent`
- construct `RemoteServiceHttpAdapter`
- construct `YahaMqttClient` with `makeBrokerTransport()`
- start component and MQTT client lifecycle
- start HTTP listener for dynamic path routing
- run until signal and perform clean shutdown
- HTTP listen false branch emits deterministic error log (`remoteservice_client[error] op=http_listen ...`).

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