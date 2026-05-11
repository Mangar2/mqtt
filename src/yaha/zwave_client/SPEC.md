# zwave_client — YAHA ZWave Runtime Types and INI Mapping

## Purpose

Defines ZWave standalone runtime config types and INI mapping behavior,
including deterministic validation errors for required fields and value ranges.

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
