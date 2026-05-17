# rs485_interface

Phase 2 scope in this module:
- MQTT <-> serial mapping helpers for RS485 command traffic
- topic/address/command resolution using rs485interface configuration
- value mapping via `interfaces` and explicit `topics` bit behavior

## Public API

### Struct Rs485MappedSerialData

Fields:
- address (serial receiver address)
- command (serial command byte)
- value (serial value 0..65535)

### Class Rs485TopicMapper

Constructor:
- `Rs485TopicMapper(const Rs485InterfaceConfig&)`

Methods:
- `toSerialData(const Message&)`: maps one MQTT command message to serial address/command/value
- `toMqttMessages(const Rs485SerialMessage&)`: maps one serial message to one or more MQTT messages

## Mapping behavior

MQTT -> serial:
- If topic exists in `topics`, use explicit command/address/value rule and add switch bits:
  - value `on` or `1` -> `value + 0x4000`
  - all other values -> `value + 0x2000`
- Otherwise:
  - resolve address by topic-prefix match in `addresses`
  - resolve command by topic-suffix match in `settings`
  - resolve value as integer or via `interfaces[...].map` for matching `usedBy` command
- Unknown address, unknown command, or unknown value mapping throws deterministic runtime error.

Serial -> MQTT:
- First evaluate explicit `topics` rules by command and sender address.
- Explicit rule value output:
  - when no switch bits present: publish `on` if mask bit set else `off`
  - when switch bits are present: publish only if mask bit set, value `on` for switch-on and `off` for switch-off
- If no explicit topic message is produced:
  - resolve topic prefix from `addresses` by sender address
  - resolve suffix from `settings` first, then `status`
  - resolve payload value via reverse lookup in `interfaces` by command and map value
- Unknown address or unknown command throws deterministic runtime error.
