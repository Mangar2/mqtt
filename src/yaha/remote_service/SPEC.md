# remote_service — YAHA RemoteService Domain Config Contract

## Purpose

Defines the RemoteService domain runtime configuration contract used by
standalone RemoteService composition.

Phase 1 scope in this module is limited to config contract types.

## Public API

### Struct `RemoteServiceConfig`

| Field | Type | Default | Notes |
|------|------|---------|-------|
| `listenHost` | `std::string` | `0.0.0.0` | HTTP listener host |
| `listenPort` | `std::uint16_t` | `9123` | HTTP listener port |
| `subscribeQos` | `Qos` | `Qos::AtLeastOnce` | FileStore monitor subscription QoS |
| `monitorTopicPrefix` | `std::string` | `$MONITOR/FileStore` | FileStore monitor subscription prefix |
| `fileStoreHost` | `std::string` | `127.0.0.1` | FileStore HTTP endpoint host |
| `fileStorePort` | `std::uint16_t` | `8210` | FileStore HTTP endpoint port |
| `mappingKeyPath` | `std::string` | empty | Required FileStore key path for service mapping payload |

## Files

| File | Role |
|------|------|
| `remote_service_config.h` | Domain config type and default constants |