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
| `tryLoadZwaveDeviceSettingsSnapshotFromFileStore` | `(const ZwaveConfig&, std::vector<ZwaveDeviceConfig>&, std::string&) -> bool` | Loads effective `devices` snapshot from FileStore for runtime reload handling |
| `tryLoadZwaveClientRuntimeConfigFromIni` | `(const IniDocument&, ZwaveClientRuntimeConfig&, std::string&) -> bool` | Maps full runtime config including `[mqtt]` |

## Standalone runtime composition

`src/yaha_zwaveclient_main.cpp` composes runtime directly:

- parse CLI: optional `<config-path>`, `--trace-messages`, `--help`
- load INI with `IniDocument::loadFromFile`
- map runtime config with `tryLoadZwaveClientRuntimeConfigFromIni`
- create `OpenZwaveRuntimeDriverPort`, bind it to `ZwaveController`, and start OpenZWave runtime (`Options`, `Manager`, watcher, `AddDriver`)
- create `ZwaveServiceComponent`
- configure `ZwaveServiceComponent` FileStore reload callback so `$MONITOR/FileStore/...` changes for matching `filestore.filename` trigger live device-config reload
- construct `YahaMqttClient` with `makeBrokerTransport()`
- run with explicit lifecycle orchestration:
  - start MQTT runtime and wait for broker connection
  - publish retained `starting` status on `$MONITOR/zwave/status`
  - when `filestore.use=true`, perform FileStore startup sync with retry policy from `[filestore]`
  - apply effective FileStore-backed device snapshot to service and run component
  - publish retained `running` status on `$MONITOR/zwave/status`
  - terminate process with non-zero exit for systemd restart when watchdog detects unresponsive ZWave input (`>=100` timeout-drop notifications and `>=3 minutes` without successful inbound ZWave input)
  - on signal/self-stop publish retained `stopped`, then close component and mqtt client

MQTT Last Will behavior:

- runtime sets Last Will on `$MONITOR/zwave/status`
- Will payload is retained `terminated`
- broker publishes `terminated` on ungraceful disconnect

OpenZWave runtime driver behavior:

- translates OpenZWave watcher notifications to `ZwaveController` callback methods
  - forwards `Type_ControllerCommand` with both node id and controller state text to preserve node-scoped include progress
  - forwards `Type_EssentialNodeQueriesComplete` as query stage `essential_queries_complete`
  - forwards `Type_NodeQueriesComplete` as query stage `queries_complete`
  - forwards `Type_NodeRemoved` and clears runtime node caches (`knownNodes`, `valueIdCache`) for the removed node
- contains all watcher callback exceptions at the callback boundary; runtime logs deterministic
  `zwave_client[error] op=watcher_notification type=<type> node=<node> detail="..."`
  lines instead of letting callback exceptions terminate the process
- maps value callbacks to normalized `ZwaveControllerValueEvent` payloads
- maps `/set` write requests to typed OpenZWave `SetValue` overloads
  - write path first uses runtime-cached ValueID from observed callbacks (node/class/instance/index)
  - if no cached ValueID exists yet, it falls back to constructed ValueID from resolved mapping fields
    - special case: class `0x26` (switch multilevel) falls back to `Byte` write type even when mapping type is `bool`
  - typed write dispatch uses the resolved OpenZWave `ValueID` type as authoritative target type
  - bool payloads (`on`/`off` mapped to `true`/`false`) are coerced by target ValueID type to avoid bool-write type mismatch:
    - bool ValueID: writes bool
    - numeric ValueID (byte/short/int/decimal): writes `1`/`0`
    - list/string ValueID: writes `on`/`off`
- routes config writes through `SetConfigParam`
- handles add/remove-failed node controller commands
- requests node state for known nodes on scan trigger
- requests all config params per configured node
- enables polling for cached value ids by node/class
- configures OpenZWave runtime `PollInterval` from `zwave.pollIntervalMs` (default `500ms`) for backend refresh cadence
- passes `zwave.pollIntervalMs` to `ZwaveController` as full-device MQTT refresh interval for all configured node ids
- passes command feedback timing settings to `ZwaveController`:
  - `zwave.commandReactionPollIntervalMs` (default `500ms`)
  - `zwave.commandReactionTimeoutMs` (default `30000ms`)
- tracks cached value ids by node/class/instance/index from OpenZWave notifications
- on value-removal notifications, prunes empty nested cache containers (index -> instance -> class -> node) to avoid retaining empty runtime cache branches
- on shutdown removes driver + watcher and destroys owned OpenZWave manager/options
- resolves OpenZWave config path in this order:
  - env `YAHA_OPENZWAVE_CONFIG_PATH`
  - `<deploy-root>/third_party/openzwave/config` (deployment default)
  - `<cwd>/third_party/openzwave/config`
  - `<cwd>/../third_party/openzwave/config`
  - `/usr/share/openzwave/config`
- resolves OpenZWave user path from `YAHA_OPENZWAVE_USER_PATH` or `<deploy-root>/tmp/openzwave`
- optional FileStore-backed device settings sync (same filestore section style as ValueService):
  - reads settings JSON from `filestore.filename` when `filestore.use=true`
  - startup sync applies the full FileStore snapshot to runtime `devices`
  - persists only the effective `devices` array back to FileStore as JSON root object (`{"devices":[...]}`)
  - startup sync is executed only after MQTT connect and `starting` status publish
  - startup sync retries use configurable count/interval keys from `[filestore]`
  - runtime monitor reload path accepts FileStore `404` as deleted-key signal and maps it to an empty effective `devices` set

Runtime startup prints a deterministic summary:

- config path
- MQTT host/port/client id
- zwave usb device/topic and configured device count
- subscribe/publish qos and retain flags
- zwave log level
- incoming/outgoing message logging flags
- command reaction poll interval and timeout

## Configuration format

Supported INI sections:

- `[mqtt]`
  - `host`, `port`, `clientId`, `reconnectDelayMs`, `keepAliveIntervalMs`, `loopSleepMs`, `logReason`
- `[zwave]`
  - `subscribeQoS`, `qos`, `retain`, `logLevel`, `logIncomingMessages`, `logOutgoingMessages`, `pollIntervalMs`, `commandReactionPollIntervalMs`, `commandReactionTimeoutMs`, `usbDevice`, `usbTopic`, `device`
- `[filestore]`
  - `use`, `host`, `port`, `filename`, `topicPrefix`, `startupRetryCount`, `startupRetryIntervalSeconds`

Device row format (`zwave.device` can appear multiple times):

- `topic|nodeId|classId|instance|index|type|label`
- Required fields: `topic`, `nodeId`
- Optional fields: `classId`, `instance`, `index`, `type`, `label`

Validation rules:

- `zwave.subscribeQoS` must be in range `0..2` when set.
- `zwave.qos` must be in range `0..2` when set.
- `zwave.retain` must be valid boolean token when set.
- `zwave.logLevel` must be in range `0..4` when set.
- `zwave.logIncomingMessages` must be valid boolean token when set.
- `zwave.logOutgoingMessages` must be valid boolean token when set.
- `zwave.pollIntervalMs` must be in range `1..60000` when set.
- `zwave.commandReactionPollIntervalMs` must be in range `1..60000` when set.
- `zwave.commandReactionTimeoutMs` must be in range `1..600000` when set.
- `filestore.port` must be in range `1..65535` when set.
- `filestore.use` must be valid boolean token when set.
- `filestore.startupRetryCount` must be in range `0..1000` when set.
- `filestore.startupRetryIntervalSeconds` must be in range `1..3600` when set.
- `zwave.usbDevice` must be present and non-empty.
- `zwave.usbTopic` must be present and non-empty.
- `zwave.device` entries are optional; when absent, startup config keeps an empty device list.
- `zwave.device` row `nodeId` must be in range `1..255`.
- Optional `classId` must be in range `0..65535` when set.
- Optional `instance` must be in range `0..255` when set.
- Optional `index` must be in range `0..255` when set.

Logging semantics:

- `logLevel=0`: disable OpenZWave protocol console output and suppress `zwave_service[event]` logs.
- `logLevel=1`: OpenZWave error logging plus important `zwave_service[event|error]` logs.
- `logLevel=2`: OpenZWave info logging plus important `zwave_service[event|error]` logs.
- `logLevel>=3`: OpenZWave detail/debug logging plus important `zwave_service[event|error]` logs.
- `logIncomingMessages` and `logOutgoingMessages` are independent MQTT trace flags and are never overridden by `logLevel`.
- `[zwave]` logging flags are parsed through shared message-log INI helper (`tryLoadMessageLogConfigFromIni`) with unchanged key names and defaults.

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
- deterministic validation failures for malformed `zwave.device` rows and missing required keys (`zwave.usbDevice`, `zwave.usbTopic`)
- combined runtime mapping of `[zwave]` and `[mqtt]` to `ZwaveClientRuntimeConfig`
- error propagation for mqtt validation failures in runtime config loading
