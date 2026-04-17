# qos — Module 5: QoS Engine

State machines for guaranteed message delivery (QoS 1 and QoS 2).
Depends on modules 1 (Data Models) and 4 (In-Memory Store).

## Sub-modules

| File(s) | Plan ref | Description |
|---------|----------|-------------|
| `qos_error.h`                                          | 5    | `QosError` enum and `QosException` |
| `packet_id_manager.h` / `packet_id_manager.cpp`        | 5.1  | Per-session packet identifier allocator; separate inbound / outbound spaces |
| `qos1_state_machine.h` / `qos1_state_machine.cpp`      | 5.2  | QoS 1 (AtLeastOnce) inbound and outbound handshake logic |
| `qos2_state_machine.h` / `qos2_state_machine.cpp`      | 5.3  | QoS 2 (ExactlyOnce) inbound and outbound handshake logic with duplicate detection |

## PacketIdManager (5.1)

Per-session, per-direction Packet Identifier tracker. One instance owns both the
inbound and outbound ID spaces for a single client session.

| Method | Description |
|--------|-------------|
| `allocate()` | Allocate the next free outbound Packet ID (5.1.1). Scans sequentially from the last-allocated ID, wraps from 65535 to 1. Throws `QosException(PacketIdExhausted)` if all 65535 IDs are in use. |
| `try_register_inbound(id)` | Reserve a client-chosen inbound Packet ID for duplicate detection. Returns `true` if the ID was new (proceed normally); `false` if already registered (duplicate). |
| `release(id, dir)` | Return a Packet ID to the free pool (5.1.2). No-op if not registered. |
| `is_in_use(id, dir)` | Return `true` if the ID is currently registered in the given direction's space (5.1.3). |
| `outbound_count()` | Number of active outbound Packet IDs. |
| `inbound_count()` | Number of active inbound Packet IDs. |

### ID allocation strategy

`allocate()` maintains a cursor (`last_id_`) and scans forward modulo 65535 (IDs are
in range [1, 65535]). On each call, the scan starts one position past the last
successfully allocated ID, guaranteeing O(1) amortised performance for typical
broker loads (far fewer than 65535 concurrent outbound exchanges per session).

## Qos1StateMachine (5.2)

Per-session QoS 1 handler. Holds references to a `PacketIdManager` and an
`InflightStore`.

| Method | Description |
|--------|-------------|
| `on_publish_received(pkt)` | Inbound (5.2.1): validates the QoS 1 PUBLISH and returns the `PubackPacket` to send. No inflight entry is created. |
| `initiate_publish(msg)` | Outbound (5.2.2): allocates a Packet ID, creates an `InflightEntry`, and returns the `PublishPacket` to send (DUP=false). |
| `on_puback_received(pkt)` | Outbound (5.2.2): completes the exchange by removing the `InflightEntry` and releasing the Packet ID. Throws `QosException(UnexpectedPacketId)` if no matching entry exists. |
| `retransmit(packet_id)` | Outbound (5.2.3): rebuilds the `PublishPacket` with DUP=true and updates the inflight timestamp. Throws `QosException(UnexpectedPacketId)` if no matching entry exists. |

## Qos2StateMachine (5.3)

Per-session QoS 2 handler. Holds references to a `PacketIdManager` and an
`InflightStore`.

### Inbound (5.3.1) — broker receives PUBLISH from publishing client

| Method | Description |
|--------|-------------|
| `on_publish_received(pkt)` | Validates PUBLISH, performs duplicate detection via `PacketIdManager::try_register_inbound`. Returns `Qos2InboundPublishResult{pubrec, is_duplicate}`. When `is_duplicate == true` the caller must not re-deliver the message. |
| `on_pubrel_received(pkt)` | Validates PUBREL, removes inflight entry, releases inbound ID. Returns the `PubcompPacket` to send. Throws `QosException(UnexpectedPacketId)` if no matching entry exists. |

### Outbound (5.3.2) — broker sends PUBLISH to subscribing client

| Method | Description |
|--------|-------------|
| `initiate_publish(msg)` | Allocates a Packet ID, creates `InflightEntry(WaitingForPubrec)`, returns `PublishPacket` to send (DUP=false). |
| `on_pubrec_received(pkt)` | Advances entry to `WaitingForPubcomp`, returns `PubrelPacket` to send. Idempotent on duplicate PUBREC — always returns PUBREL. Throws `QosException(UnexpectedPacketId)` if no matching entry exists. |
| `on_pubcomp_received(pkt)` | Completes the exchange: removes inflight entry, releases Packet ID. Throws `QosException(UnexpectedPacketId)` if no matching entry exists. |
| `retransmit(packet_id)` | Returns `std::variant<PublishPacket, PubrelPacket>` (5.3.4): PUBLISH with DUP=true when in `WaitingForPubrec` state, PUBREL when in `WaitingForPubcomp` state. Updates inflight timestamp. Throws `QosException(UnexpectedPacketId)` if no matching entry exists. |

### Duplicate detection (5.3.3)

`on_publish_received` consults `PacketIdManager::try_register_inbound` to detect
re-delivered QoS 2 PUBLISH packets. If the inbound Packet ID is already registered,
the message is flagged as a duplicate and the caller skips re-delivery; PUBREC is still
returned to unblock the client's state machine.
