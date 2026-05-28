# pushover_client — Pushover Runtime Config and Sender Wiring

## Purpose

Defines standalone Pushover runtime config model, INI mapping, and default
HTTP sender factory used by `yahapushoverclient`.

## Public API

### Struct `PushoverClientRuntimeConfig`

| Field | Type | Meaning |
|------|------|---------|
| `pushoverConfig` | `PushoverConfig` | Pushover domain config |
| `mqttConfig` | `YahaMqttClient::Config` | Generic MQTT runtime config |

### Functions

| Function | Signature | Behavior |
|---------|-----------|----------|
| `tryLoadPushoverConfigFromIni` | `(const IniDocument&, PushoverConfig&, std::string&) -> bool` | Parses `[pushover]`, repeated `[device]`, repeated `[subscription]` |
| `tryLoadPushoverClientRuntimeConfigFromIni` | `(const IniDocument&, PushoverClientRuntimeConfig&, std::string&) -> bool` | Parses domain + mqtt config |
| `makePushoverRequestSender` | `(const PushoverConfig&) -> PushoverRequestSender` | Builds HTTPS POST sender |

## Configuration model

### Section `[pushover]`

- `host` (optional, default `api.pushover.net`)
- `path` (optional, default `/1/messages.json`)
- `port` (optional, default `443`, range `1..65535`)
- `token` (required)
- `user` (required)

### Repeated section `[device]`

Each device must be defined with key:

- `name`

### Repeated section `[subscription]`

Each subscription entry must contain:

- `topic`
- `qos` (`0`, `1`, `2`)

Validation behavior:

- missing required Pushover keys fail config loading
- missing `[device]` entries fail config loading
- missing `[subscription]` entries fail config loading
- unknown keys in `[device]` or `[subscription]` fail config loading

## Files

| File | Role |
|------|------|
| `pushover_client_app.h` | Runtime config and sender declarations |
| `pushover_client_app.cpp` | INI mapping and sender implementation |
| `test/TEST_SPEC.md` | Unit-test specification |
| `test/pushover_client_app_test.cpp` | Unit tests |
