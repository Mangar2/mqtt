# ZWave Device Entry Syntax and Semantics

## Purpose

This specification defines the exact syntax and runtime semantics of `device` entries in `[zwave]` INI configuration.

It is intended for UI generation and validation tooling that must present, edit, and reason about ZWave topic mappings deterministically.

## Configuration line format

Each entry uses one INI key with pipe-separated fields:

`device = topic|nodeId|classId|instance|index|type|label`

The key may appear multiple times.

## Field contract

Position 1 `topic`:
- required, non-empty string
- MQTT base topic for this mapping row

Position 2 `nodeId`:
- required integer in range `1..255`

Position 3 `classId`:
- optional integer in range `0..65535`
- empty means wildcard for outgoing mapping and deferred resolution for incoming mapping

Position 4 `instance`:
- optional integer in range `0..255`
- empty means wildcard on outgoing side
- incoming default when unresolved: `1`

Position 5 `index`:
- optional integer in range `0..255`
- empty means wildcard on outgoing side
- incoming default when unresolved: `0`

Position 6 `type`:
- optional string
- incoming default when unresolved:
  - `switch` for `classId=0x25`
  - `byte` for `classId=0x26`
  - `bool` otherwise

Position 7 `label`:
- optional string
- used for incoming resolution when `classId` is not configured

## Semantic model

One row represents:
- `topic` -> identifier selector
- selector = `(nodeId, classId?, instance?, index?, type?, label?)`

The selector may be broad (only `nodeId`) or specific (all fields set).

Broad rows are valid and intentionally act as a catch-all within one node.

## Outgoing mapping semantics (ZWave value to MQTT topic)

Outgoing mapping consumes a value descriptor `(nodeId, classId, instance, index, label?)` and selects one best row.

Candidate rule:
- row is candidate when:
  - row.nodeId equals descriptor.nodeId
  - and each configured optional field equals descriptor field
  - non-configured optional fields are treated as wildcard

Specificity scoring:
- base match: `+1`
- class exact match: `+1`
- index exact match: `+2`
- instance exact match: `+4`

Winner rule:
- candidate with highest score wins
- if no row matches, mapping fails and caller uses fallback topic behavior

Label append rule:
- if winning row has no configured `classId` and descriptor has label, output topic is:
  - `<row.topic>/<descriptor.label>`
- otherwise output topic is `<row.topic>`

### Interpretation for UI

`topic|nodeId` means:
- one wildcard mapping for all values of this node
- it is less specific than rows that additionally constrain class/instance/index
- more specific rows should be displayed as overrides of the broad row

## Incoming mapping semantics (MQTT set topic to ZWave target)

Incoming mapping consumes:
- command topic without trailing `/set`
- optional object label extracted from topic path

Lookup order:
1. `<topic>/<label>` when label exists
2. `<topic>`

Resolution rules:
- if row contains `classId`, it is used directly
- if row has no `classId`, resolver must find class data in node runtime objects by matching label and instance
- missing `instance` and `index` are defaulted to `1` and `0`
- missing `type` follows default rules defined above

## Precedence example from configuration

Given:

`device = ground/wardrobe/zwave/sys/ventilation|6`

`device = ground/wardrobe/zwave/switch/ventilation|6|37|||switch`

Semantics:
1. First row is broad catch-all for node `6`.
2. Second row is specific override for class `37` (`0x25`, binary switch).
3. Outgoing events from node `6`, class `37` match both rows, but second row wins by specificity.
4. Outgoing events from node `6` with other classes use first row.
5. Incoming command to `ground/wardrobe/zwave/switch/ventilation/set` resolves directly to class `37` and type `switch`.

## UI authoring guidance

1. Render rows as ordered rules grouped by `nodeId`.
2. Mark broad rows (`nodeId` only) as base mappings.
3. Mark class/instance/index-constrained rows as overrides.
4. Warn when two rows have identical selector specificity and overlapping selector scope.
5. Show effective resolved target preview for sample inputs.
6. Validate numeric ranges and empty-field semantics before write.
7. Preserve row order and exact text for unchanged rows.

## Validation errors

Configuration is invalid when:
- `topic` is empty
- `nodeId` missing or outside `1..255`
- optional numeric field exists but outside allowed range
- row has fewer than 2 or more than 7 fields

## Compatibility notes

- This syntax is source-of-truth for current C++ runtime.
- Empty optional fields are semantic wildcards, not null values.
- A broad row and specific row with same node are expected and represent intentional override layering.
