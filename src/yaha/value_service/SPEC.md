# value_service — YAHA ValueService Config Contract

## Purpose

Defines the ValueService domain runtime configuration contract used by
ValueService standalone composition and upcoming component behavior implementation.

Phase 1 scope in this module is config contract only.

## Public API

### Struct `ValueServiceConfig`

| Field | Type | Default | Notes |
|------|------|---------|-------|
| `monitorTopicPrefix` | `std::string` | `$MONITOR/FileStore` | FileStore monitor topic prefix used for reload subscriptions |
| `valuesKeyPath` | `std::string` | `/valueservice/values` | FileStore key path for full value map load/save |
| `fileStoreHost` | `std::string` | `127.0.0.1` | FileStore HTTP host |
| `fileStorePort` | `std::uint16_t` | `8210` | FileStore HTTP port |
| `fileStoreEnabled` | `bool` | `true` | Enables FileStore-based startup load and write-back |
| `subscribeQos` | `Qos` | `Qos::AtLeastOnce` | Subscription and outbound value publish QoS |
| `legacyValuesFileName` | `std::string` | empty | Legacy migration config key; runtime local-file persistence is disabled |

## Files

| File | Role |
|------|------|
| `value_service_component.h` | ValueService config contract declarations |

## Implementation notes

- Default numeric configuration values are exposed as named constants in
	`value_service_component.h` to avoid magic-number literals in struct defaults.
