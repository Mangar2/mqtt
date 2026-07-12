# opensensemap_client — OpenSenseMap Runtime Config and HTTP Sender Wiring

## Purpose

Defines standalone OpenSenseMap runtime config model, INI mapping, and default
HTTP request sender factory used by `yahaopensensemapclient`.

## Public API

### Struct `OpenSenseMapClientRuntimeConfig`

| Field | Type | Meaning |
|------|------|---------|
| `openSenseMapConfig` | `OpenSenseMapConfig` | Domain config for OpenSenseMap component |
| `mqttConfig` | `YahaMqttClient::Config` | Generic MQTT runtime config |

### Functions

| Function | Signature | Behavior |
|---------|-----------|----------|
| `tryLoadOpenSenseMapConfigFromIni` | `(const IniDocument&, OpenSenseMapConfig&, std::string&) -> bool` | Parses `[opensensemap]` and repeated `[sensor]` blocks |
| `tryLoadOpenSenseMapClientRuntimeConfigFromIni` | `(const IniDocument&, OpenSenseMapClientRuntimeConfig&, std::string&) -> bool` | Parses domain + mqtt config |
| `makeOpenSenseMapRequestSender` | `(const OpenSenseMapConfig&) -> OpenSenseMapRequestSender` | Builds HTTP/HTTPS POST sender |

## Configuration model

### Section `[opensensemap]`

- `station` (optional)
- `id` (required)
- `host` (optional, default `ingress.opensensemap.org`)
- `port` (optional, default `443`, range `1..65535`)
- `qos` (optional, default `1`, range `0..2`)
- `useTls` (optional, default `true`)
- `logIncomingMessages` (optional, default `false`) — parsed into `openSenseMapConfig.logIncomingMessages`,
  makes the component log every incoming message to stdout regardless of guard/error outcome
- `logReason` (optional, default `true`) — parsed into `mqttConfig.logReason`, controls reason-chain
  detail for the generic `mqttConfig.enableMessageTrace` trace (only active with the `--trace-messages`
  CLI flag; independent of `logIncomingMessages`)

### Repeated section `[sensor]`

Each sensor must be defined in a dedicated section and must contain exactly:

- `name`
- `unit`
- `topic`
- `id`

Optional key per sensor:

- `minUploadIntervalSeconds` (optional, default `0`, range `0..4294967295`)

Validation behavior:

- key `uint` is rejected explicitly with message to use `unit`
- missing required fields fail config loading
- missing sensor sections fail config loading

## Files

| File | Role |
|------|------|
| `opensensemap_client_app.h` | Runtime config and sender factory declarations |
| `opensensemap_client_app.cpp` | INI mapping and sender implementation |
| `test/TEST_SPEC.md` | Unit-test specification |
| `test/opensensemap_client_app_test.cpp` | Unit tests |
