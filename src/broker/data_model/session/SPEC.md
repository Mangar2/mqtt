# session — Module 1.7

Pure data structures for MQTT 5.0 session state and in-flight message tracking.
No logic, no I/O, no external dependencies. All types live in the `mqtt` namespace.

## Files

| File | Plan ref | Contents |
|------|----------|----------|
| `inflight_direction.h` | 1.7.2 | `InflightDirection` enum |
| `inflight_state.h`     | 1.7.2 | `InflightState` enum |
| `inflight_entry.h`     | 1.7.2 | `InflightEntry` struct |
| `session_state.h`      | 1.7.1 | `SessionState` struct |

## Types

### InflightDirection (1.7.2)

Indicates the direction of a QoS 1 or QoS 2 handshake:

| Value | Name | Meaning |
|-------|------|---------|
| 0 | `Inbound` | Message received from a publishing client; broker is acknowledging. |
| 1 | `Outbound` | Message being sent to a subscribing client; client is acknowledging. |

### InflightState (1.7.2)

Current phase of a QoS handshake:

| Value | Name | Phase |
|-------|------|-------|
| 0 | `WaitingForPuback`  | QoS 1 outbound: PUBLISH sent; awaiting PUBACK. |
| 1 | `WaitingForPubrec`  | QoS 2 outbound: PUBLISH sent; awaiting PUBREC. |
| 2 | `WaitingForPubrel`  | QoS 2 inbound:  PUBREC sent; awaiting PUBREL from client. |
| 3 | `WaitingForPubcomp` | QoS 2 outbound: PUBREL sent; awaiting PUBCOMP. |

### InflightEntry (1.7.2)

Represents one pending QoS 1 or QoS 2 handshake stored by the Inflight Store (Module 4.4):

- `packet_id` (uint16_t, default 0): Packet Identifier; unique within the session's direction.
- `message` (Message): Copy of the message being exchanged.
- `qos` (QoS, default `AtLeastOnce`): QoS level of this exchange (1 or 2).
- `state` (InflightState, default `WaitingForPuback`): Current handshake phase.
- `direction` (InflightDirection, default `Outbound`): Direction of the exchange.
- `timestamp` (std::chrono::steady_clock::time_point, default epoch): Time of last transmission;
  used by the QoS Engine (Module 5) for retransmission scheduling.

Supports `operator==` via `= default`.

### SessionState (1.7.1)

Persistent broker-side state for a single client session, owned by the Session Store (Module 4.3)
and the Session Manager (Module 10):

- `client_id` (Utf8String): Unique client identifier.
- `subscriptions` (std::vector\<Subscription\>): Active subscriptions held by this session.
- `session_expiry_interval` (uint32_t, default 0): Session lifetime after disconnect, in seconds.
  - `0` — session expires immediately on disconnect (Clean Session behaviour).
  - `0xFFFF'FFFF` — session never expires.

Supports `operator==` via `= default`.

## Design rules

- Header-only; one struct/enum per file.
- `#pragma once` on every header.
- `operator==` provided via `= default` on every struct.
- `session_state.h` depends on `data_model/subscription/subscription.h`.
- `inflight_entry.h` depends on `data_model/message/message.h`.
