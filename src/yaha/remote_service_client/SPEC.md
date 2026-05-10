# remote_service_client — YAHA RemoteService Runtime Types and INI Mapping

## Purpose

Defines RemoteService standalone runtime config data types and RemoteService
specific INI mapping behavior.

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

## Files

| File | Role |
|------|------|
| `remote_service_client_app.h` | Runtime config declarations and loader signatures |
| `remote_service_client_app.cpp` | Runtime config parsing and validation implementation |