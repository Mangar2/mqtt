# zwave_client — YAHA ZWave Runtime Mapping and Standalone Composition

## Purpose

Defines ZWave standalone runtime config types, deterministic INI mapping behavior,
and phase-4 standalone composition entrypoint wiring.

## Public API

### Struct ZwaveClientRuntimeConfig

| Field | Type | Notes |
|------|------|-------|
| `zwaveConfig` | `ZwaveConfig` | ZWave domain settings |
| `mqttConfig` | `YahaMqttClient::Config` | Generic MQTT runtime settings |

### Config mapping helpers

| Function | Signature | Notes |
|---------|-----------|-------|
| `tryLoadZwaveConfigFromIni` | `(const IniDocument&, ZwaveConfig&, std::string&) -> bool` | Maps `[zwave]` fields into domain config with validation |
| `tryLoadZwaveClientRuntimeConfigFromIni` | `(const IniDocument&, ZwaveClientRuntimeConfig&, std::string&) -> bool` | Maps full runtime config including `[mqtt]` |

## Standalone runtime composition

`src/yaha_zwaveclient_main.cpp` composes runtime directly:

- parse CLI: optional `<config-path>`, `--trace-messages`, `--help`
- load INI with `IniDocument::loadFromFile`
- map runtime config with `tryLoadZwaveClientRuntimeConfigFromIni`
- create `OpenZwaveRuntimeDriverPort`, bind it to `ZwaveController`, and start OpenZWave runtime (`Options`, `Manager`, watcher, `AddDriver`)
- create `ZwaveServiceComponent`
- construct `YahaMqttClient` with `makeBrokerTransport()`
- run until shutdown via `YahaMqttClientRuntime`

OpenZWave runtime driver behavior:

- translates OpenZWave watcher notifications to `ZwaveController` callback methods
- maps value callbacks to normalized `ZwaveControllerValueEvent` payloads
- maps `/set` write requests to typed OpenZWave `SetValue` overloads
- routes config writes through `SetConfigParam`
- handles add/remove-failed node controller commands
- requests node state for known nodes on scan trigger
- requests all config params per configured node
- enables polling for cached value ids by node/class
- on shutdown removes driver + watcher and destroys owned OpenZWave manager/options

Runtime startup prints a deterministic summary:

- config path
- MQTT host/port/client id
- zwave usb device/topic and configured device count
- subscribe/publish qos and retain flags

## Configuration format

Supported INI sections:

- `[mqtt]`
  - `host`, `port`, `clientId`, `reconnectDelayMs`, `keepAliveIntervalMs`, `loopSleepMs`, `logReason`
- `[zwave]`
  - `subscribeQoS`, `qos`, `retain`, `usbDevice`, `usbTopic`, `device`

Device row format (`zwave.device` can appear multiple times):

- `topic|nodeId|classId|instance|index|type|label`
- Required fields: `topic`, `nodeId`
- Optional fields: `classId`, `instance`, `index`, `type`, `label`

Validation rules:

- `zwave.subscribeQoS` must be in range `0..2` when set.
- `zwave.qos` must be in range `0..2` when set.
- `zwave.retain` must be valid boolean token when set.
- `zwave.usbDevice` must be present and non-empty.
- `zwave.usbTopic` must be present and non-empty.
- At least one `zwave.device` entry must be present.
- `zwave.device` row `nodeId` must be in range `1..255`.
- Optional `classId` must be in range `0..65535` when set.
- Optional `instance` must be in range `0..255` when set.
- Optional `index` must be in range `0..255` when set.

## Files

| File | Role |
|------|------|
| `zwave_client_app.h` | Runtime config declarations and loader signatures |
| `zwave_client_app.cpp` | Runtime config parsing and validation implementation |
| `openzwave_runtime_driver_port.h` | OpenZWave-backed `IZwaveDriverPort` declaration |
| `openzwave_runtime_driver_port.cpp` | OpenZWave runtime lifecycle, watcher translation, and typed write implementation |
| `../yaha_zwaveclient_main.cpp` | Standalone runtime entrypoint and composition wiring |

## Phase-6 test coverage

Unit/runtime-integration verification for this module is provided by:

- `test/TEST_SPEC.md`
- `test/zwave_client_app_test.cpp`

Covered phase-6 behavior:

- config schema/default validation for `[zwave]` mapping
- deterministic validation failures for malformed `zwave.device` rows and missing required keys
- combined runtime mapping of `[zwave]` and `[mqtt]` to `ZwaveClientRuntimeConfig`
- error propagation for mqtt validation failures in runtime config loading
