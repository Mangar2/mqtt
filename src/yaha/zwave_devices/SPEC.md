# zwave_devices — YAHA ZWave Mapping and Conversion Helpers

## Purpose

Provides deterministic mapping and conversion helpers used by the ZWave runtime
for bridging between MQTT topic space and ZWave value identifiers.

## Public API

### Struct ZwaveValueDescriptor

| Field | Type | Notes |
|------|------|-------|
| `nodeId` | `std::uint16_t` | ZWave node id |
| `classId` | `std::uint16_t` | ZWave command class id |
| `instance` | `std::uint8_t` | ZWave instance id |
| `index` | `std::uint8_t` | ZWave value index |
| `label` | `std::optional<std::string>` | Optional label used for topic expansion |
| `valueId` | `std::optional<std::uint64_t>` | Optional value-id cache key |

### Struct ZwaveTopicMapping

| Field | Type | Notes |
|------|------|-------|
| `topic` | `std::string` | Resolved outbound MQTT topic |
| `type` | `std::string` | Resolved output type hint |

### Struct ZwaveNodeObject

| Field | Type | Notes |
|------|------|-------|
| `classId` | `std::uint16_t` | Command class id |
| `label` | `std::string` | Value label |
| `instance` | `std::uint8_t` | Instance id |
| `index` | `std::uint8_t` | Value index |
| `type` | `std::string` | Type hint |

### Struct ZwaveResolvedId

| Field | Type | Notes |
|------|------|-------|
| `nodeId` | `std::uint16_t` | Resolved node id |
| `classId` | `std::uint16_t` | Resolved class id |
| `instance` | `std::uint8_t` | Resolved instance (default `1`) |
| `index` | `std::uint8_t` | Resolved index (default `0`) |
| `type` | `std::string` | Resolved type (default `bool`) |

### Enum ZwaveWriteKind

| Value | Meaning |
|------|---------|
| `SetValue` | Regular ZWave set-value path |
| `SetConfigParam` | Configuration parameter set path (`classId == 0x70`) |

### Struct ZwaveWriteRequest

| Field | Type | Notes |
|------|------|-------|
| `kind` | `ZwaveWriteKind` | Write operation kind |
| `target` | `ZwaveResolvedId` | Resolved target metadata |
| `value` | `std::variant<bool, double, std::string>` | Converted write payload |

### Class ZwaveDevicesMapper

| Function | Signature | Notes |
|---------|-----------|-------|
| constructor | `(std::vector<ZwaveDeviceConfig>)` | Stores mapping table |
| `valueToTopicAndType` | `(const ZwaveValueDescriptor&) -> std::optional<ZwaveTopicMapping>` | Best-match mapping with value-id cache support |
| `topicToZwaveId` | `(const ZwaveNodeMap&, const std::string&, const std::optional<std::string>&) -> ZwaveResolvedId` | Topic/label resolution with default completion and class lookup by label |
| `buildWriteRequest` | `(const ZwaveResolvedId&, const Value&) -> ZwaveWriteRequest` | Converts incoming MQTT value to normalized write payload |

## Behavior

### valueToTopicAndType

- Filters candidates by node id and optional class/instance/index constraints.
- Calculates specificity score:
  - base match `1`
  - class exact `+1`
  - index exact `+2`
  - instance exact `+4`
- Returns highest score mapping.
- If selected mapping has no configured class id and descriptor has label:
  - appends `/label` to the configured topic.
- Caches mapping by `valueId` when present.

### topicToZwaveId

- Lookup order:
  - first `<topic>/<label>` when label exists
  - then `<topic>`.
- Completes defaults when missing from configuration:
  - `instance=1`, `index=0`, `type=bool`.
- If configured class id is missing:
  - requires label,
  - resolves by searching node objects with matching label and instance,
  - copies `classId`, `index`, and `type` from matched object.

### buildWriteRequest

- `classId == 0x70` routes to `SetConfigParam` and numeric conversion.
- type `bool` or `switch` uses parity truth table:
  - `on`, `1`, `true`, numeric `1` => `true`
  - all other values => `false`
- type `byte` converts to numeric payload.
- other types keep number payloads as numeric and string payloads as string.

## Files

| File | Role |
|------|------|
| `zwave_devices_mapper.h` | Mapping and conversion declarations |
| `zwave_devices_mapper.cpp` | Mapping and conversion implementation |
| `test/TEST_SPEC.md` | Mapping parity test matrix for phase-5 coverage |
| `test/zwave_devices_mapper_test.cpp` | Unit tests for scoring, label append, class lookup, and write conversion behavior |
