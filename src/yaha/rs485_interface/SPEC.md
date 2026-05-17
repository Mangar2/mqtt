# rs485_interface

Phase 2/4/6 scope in this module:
- MQTT <-> serial mapping helpers for RS485 command traffic
- topic/address/command resolution using rs485interface configuration
- value mapping via `interfaces` and explicit `topics` bit behavior
- RS485 MQTT component runtime boundary (`IMqttComponent`) for standalone process wiring

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

### Class Rs485InterfaceComponent

Constructor:
- `Rs485InterfaceComponent(Rs485InterfaceConfig)`

Methods:
- `getSubscriptions()`: derives wildcard `/set` subscriptions from `addresses` + `settings`, adds explicit `topic/+`, and trace topics
- `handleMessage(const Message&)`: handles trace-level updates and dispatches `/set`, `/temporary`, `/blink` actions
- `run()`: starts scheduler tick and periodic time-of-day command loop threads
- `close()`: stops loops and joins action worker threads
- `setPublishCallback(PublishCallback)`: sets MQTT publish callback sink
- `setSerialSendCallback(SerialSendCallback)`: sets encoded serial output callback sink
- `feedSerialBytes(const std::vector<uint8_t>&)`: decodes serial stream, routes through scheduler, maps publish messages

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

## Component behavior

- `/set`: mapped immediately via `Rs485TopicMapper::toSerialData` and enqueued to scheduler as reply-expected packet.
- `/temporary`: sends `on`, waits configured or payload seconds, then sends `off`.
- `/blink`: toggles cached topic state with configured blink delay and payload cycle count.
- Scheduler send callback encodes frames with protocol codec and emits bytes via serial send callback.
- Time-of-day loop periodically sends broadcast command `'C'` with local minutes-of-day payload.
- Serial receive pipeline uses `Rs485StreamReader`, scheduler token processing, and publishes only messages accepted by scheduler state.
- scheduler, time-of-day, temporary, and blink waits are interruptible by `close()` so shutdown does not block for full configured delay windows.
- phase-6 tests verify subscription contract, action-to-serial emission, serial-to-publish mapping, and trace-topic handling.
