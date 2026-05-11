# zwave — YAHA ZWave Domain Configuration Contract

## Purpose

Defines the ZWave domain-level runtime configuration model used by the ZWave
standalone process and later component/runtime phases.

## Public API

### Struct ZwaveUsbConfig

| Field | Type | Notes |
|------|------|-------|
| `device` | `std::string` | USB device path or identifier |
| `topic` | `std::string` | MQTT topic for USB/controller reporting |

### Struct ZwaveDeviceConfig

| Field | Type | Notes |
|------|------|-------|
| `topic` | `std::string` | MQTT topic prefix for this device |
| `nodeId` | `std::uint16_t` | ZWave node id |
| `classId` | `std::optional<std::uint16_t>` | Optional command class id |
| `instance` | `std::optional<std::uint8_t>` | Optional instance id |
| `index` | `std::optional<std::uint8_t>` | Optional value index |
| `type` | `std::optional<std::string>` | Optional value type hint |
| `label` | `std::optional<std::string>` | Optional label hint |

### Struct ZwaveConfig

| Field | Type | Default | Notes |
|------|------|---------|-------|
| `subscribeQos` | `Qos` | `Qos::AtLeastOnce` | MQTT subscribe QoS for inbound commands |
| `qos` | `Qos` | `Qos::AtLeastOnce` | MQTT publish QoS for outbound state updates |
| `retain` | `bool` | `false` | MQTT retain flag for outbound state updates |
| `usb` | `ZwaveUsbConfig` | empty fields | USB/controller endpoint configuration |
| `devices` | `std::vector<ZwaveDeviceConfig>` | empty | Required list of configured device mappings |

## Files

| File | Role |
|------|------|
| `zwave_config.h` | Domain configuration types for ZWave runtime |
