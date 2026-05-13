# value_service — YAHA ValueService Component

## Purpose

Defines ValueService domain runtime configuration and phase 2 component behavior
for FileStore-backed value-map lifecycle.

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

### Class `ValueServiceComponent` : `IMqttComponent`

| Member | Signature | Notes |
|------|------|-------|
| ctor | `explicit ValueServiceComponent(ValueServiceConfig)` | Stores configuration |
| `getSubscriptions` | `SubscriptionMap() const` | Includes monitor prefix and dynamic `<key>/set` subscriptions |
| `handleMessage` | `void(const Message&)` | Handles monitor reload and `/set` updates |
| `run` | `void()` | Optional startup FileStore load and retained replay publish |
| `close` | `void()` | Stops lifecycle flag |
| `setPublishCallback` | `void(PublishCallback)` | Stores callback for retained value publishes |
| `isRunning` | `bool() const` | Lifecycle state helper |
| `valueCount` | `size_t() const` | Diagnostic helper |
| `valueForKey` | `std::optional<Value>(const std::string&) const` | Diagnostic helper |

## Behavior

- Startup `run()`:
	- sets running state
	- if FileStore enabled, loads full map from configured key path
	- publishes retained replay for all loaded keys
	- each startup replay message carries reason `loaded from valuestore on startup`
- `/set` handling:
	- accepts `<key>/set` topics
	- accepts only string and integral numeric values
	- updates in-memory map and publishes retained `<key>` state message
	- persists full map to FileStore via HTTP POST (failure does not block publish and emits structured error log)
- Monitor reload handling:
	- listens to `<monitorTopicPrefix>/#`
	- reloads from FileStore when payload `keyPath` matches `valuesKeyPath`
	- on successful reload publishes full retained replay
	- on reload failure emits structured error log
	- each reload replay message carries reason `reloaded after valuestore file change`
- Publish handling:
	- outgoing retained publish is delivery-result aware via `PublishCallback`
	- success log is emitted only after callback confirms send
	- failures emit structured `value_service[out-fail]` log with category and reason
	- failed publishes are queued in bounded retry queue and retried on later activity cycles
	- exhausted retry budget emits `category=retry_exhausted` and drops message
- Subscription synchronization (mandatory invariant):
	- the broker subscription set for `<key>/set` topics must always match the current in-memory key set exactly
	- whenever keys change (startup load, monitor-triggered reload, or runtime key add/remove/change), subscriptions must be updated immediately to the new key set
	- stale subscriptions for removed keys must be removed, and missing subscriptions for new keys must be added
	- this is mandatory in every case; no delayed or best-effort synchronization is allowed
- Message logging:
	- logs every inbound message to `std::cout` before handling (`value_service[in] ...`)
	- logs every outbound published message to `std::cout` only after callback success (`value_service[out] ...`)
	- logs outbound failures as `value_service[out-fail] ...`
- Persistence format:
	- full JSON object map `key -> value`
	- values restricted to `string` or integer numbers

## Files

| File | Role |
|------|------|
| `value_service_component.h` | ValueService config and component declarations |
| `value_service_component.cpp` | ValueService phase 2 runtime behavior implementation |

## Implementation notes

- Default numeric configuration values are exposed as named constants in
	`value_service_component.h` to avoid magic-number literals in struct defaults.
