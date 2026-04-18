# client_session — Module 21: Client Session Context

Bundles per-connection runtime state and packet-level handlers into one object.
Depends on modules 2 (codec), 5 (QoS), 7 (connection primitives), and 20 (outbound queue).

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `client_session.h/.cpp` | 21 | `ClientSession` owning per-client state and handler methods for MQTT packet flow |

## `ClientSession`

`ClientSession` is created for exactly one connected client. It owns the session-local
state machines and utilities that previously would be scattered across a large handler function.

Owned state:

- `PacketIdManager`
- `Qos1StateMachine`
- `Qos2StateMachine`
- `ReceiveMaximum`
- `TopicAliasTable`
- `KeepAliveTimer`
- `ConnectionStateMachine`
- `EnhancedAuthHandler`
- shared `OutboundQueue`
- copied `client_id` and `username`

### Public API

- `on_publish(PublishPacket) -> InboundPublishResult`
  - Converts inbound PUBLISH into a routable `Message`.
  - QoS 0: returns no ACK frames.
  - QoS 1: returns encoded PUBACK.
  - QoS 2: returns encoded PUBREC and duplicate flag handling via `Qos2StateMachine`.
- `on_puback(PubackPacket)`
  - Forwards to `Qos1StateMachine` and releases one `ReceiveMaximum` slot.
- `on_pubrec(PubrecPacket) -> WriteBuffer`
  - Forwards to `Qos2StateMachine`, returns encoded PUBREL.
- `on_pubrel(PubrelPacket) -> WriteBuffer`
  - Forwards to `Qos2StateMachine`, returns encoded PUBCOMP.
- `on_pubcomp(PubcompPacket)`
  - Forwards to `Qos2StateMachine` and releases one `ReceiveMaximum` slot.
- `on_auth(AuthPacket) -> AuthResult`
  - Forwards AUTH packets to `EnhancedAuthHandler`.
  - Uses `reauthenticate()` when reason code is `ReAuthenticate`.
- `drain_outbound() -> vector<WriteBuffer>`
  - Drains queued outbound `Message` values from the per-client `OutboundQueue`.
  - Encodes QoS 0 messages directly as PUBLISH.
  - For QoS 1/2, enforces `ReceiveMaximum` and starts the corresponding QoS state machine.
  - Preserves order by parking blocked QoS 1/2 messages in an internal deferred FIFO.

### Notes

- Thread safety: `ClientSession` itself is not thread-safe and must be used from one owning client thread.
- `OutboundQueue` remains thread-safe and may be pushed from other threads.
- The constructor transitions the internal `ConnectionStateMachine` to `Connected` because this context is created after CONNECT succeeds.
