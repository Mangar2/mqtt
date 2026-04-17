# qos/test — Unit Tests for Module 5: QoS Engine

## Test cases

### PacketIdManager (5.1)

| Test case | Tag | Description |
|-----------|-----|-------------|
| `allocate_returns_nonzero_ids` | `[packet_id_manager]` | Allocated IDs are non-zero and in [1, 65535]. |
| `allocate_sequential_ids` | `[packet_id_manager]` | IDs advance sequentially from 1. |
| `allocate_wraps_around` | `[packet_id_manager]` | After releasing ID 65535, the next allocation wraps to an earlier ID. |
| `allocate_exhausted_throws` | `[packet_id_manager]` | Throws `PacketIdExhausted` when all 65535 outbound IDs are in use. |
| `release_allows_reuse` | `[packet_id_manager]` | Releasing an ID allows it to be re-allocated. |
| `separate_inbound_outbound_spaces` | `[packet_id_manager]` | Same numeric ID may be active in both inbound and outbound spaces simultaneously. |
| `is_in_use_reflects_allocate_and_release` | `[packet_id_manager]` | `is_in_use` returns true after allocate and false after release. |
| `try_register_inbound_new_id` | `[packet_id_manager]` | Returns true for a previously unseen inbound ID. |
| `try_register_inbound_duplicate` | `[packet_id_manager]` | Returns false when the same ID is registered a second time. |
| `counts_reflect_allocations` | `[packet_id_manager]` | `outbound_count` / `inbound_count` reflect the number of active IDs. |

### Qos1StateMachine (5.2)

| Test case | Tag | Description |
|-----------|-----|-------------|
| `on_publish_received_returns_puback` | `[qos1]` | Valid QoS 1 PUBLISH produces a PUBACK with the same packet_id. |
| `on_publish_received_invalid_qos_throws` | `[qos1]` | QoS 0 or QoS 2 PUBLISH throws `InvalidPacket`. |
| `on_publish_received_missing_packet_id_throws` | `[qos1]` | PUBLISH without packet_id throws `InvalidPacket`. |
| `initiate_publish_allocates_id` | `[qos1]` | `initiate_publish` allocates a Packet ID and marks it as in use. |
| `initiate_publish_creates_inflight_entry` | `[qos1]` | An `InflightEntry` with `WaitingForPuback` state is created in the store. |
| `initiate_publish_returns_correct_packet` | `[qos1]` | Returned PUBLISH has DUP=false, correct QoS, topic, payload. |
| `initiate_publish_invalid_qos_throws` | `[qos1]` | Message with QoS != AtLeastOnce throws `InvalidPacket`. |
| `on_puback_received_removes_entry` | `[qos1]` | Matching PUBACK removes the inflight entry and releases the ID. |
| `on_puback_received_unknown_id_throws` | `[qos1]` | PUBACK for unknown packet_id throws `UnexpectedPacketId`. |
| `retransmit_sets_dup_flag` | `[qos1]` | `retransmit` rebuilds the PUBLISH with DUP=true and correct fields. |
| `retransmit_updates_timestamp` | `[qos1]` | Entry timestamp changes after `retransmit`. |
| `retransmit_unknown_id_throws` | `[qos1]` | `retransmit` for unknown packet_id throws `UnexpectedPacketId`. |

### Qos2StateMachine (5.3)

| Test case | Tag | Description |
|-----------|-----|-------------|
| `inbound_publish_creates_entry_and_returns_pubrec` | `[qos2]` | First PUBLISH creates `InflightEntry(WaitingForPubrel)` and returns PUBREC with correct packet_id. |
| `inbound_publish_duplicate_detected` | `[qos2]` | Second PUBLISH with the same packet_id returns `is_duplicate=true` without creating a second entry. |
| `inbound_publish_invalid_qos_throws` | `[qos2]` | Non-QoS-2 PUBLISH throws `InvalidPacket`. |
| `inbound_publish_missing_packet_id_throws` | `[qos2]` | PUBLISH without packet_id throws `InvalidPacket`. |
| `on_pubrel_returns_pubcomp` | `[qos2]` | PUBREL triggers removal of inbound entry and returns PUBCOMP with same packet_id. |
| `on_pubrel_unknown_id_throws` | `[qos2]` | PUBREL for unknown inbound packet_id throws `UnexpectedPacketId`. |
| `outbound_initiate_publish_creates_entry` | `[qos2]` | `initiate_publish` allocates ID, creates `InflightEntry(WaitingForPubrec)`, returns correct PUBLISH. |
| `outbound_initiate_publish_invalid_qos_throws` | `[qos2]` | Message with QoS != ExactlyOnce throws `InvalidPacket`. |
| `on_pubrec_advances_state_and_returns_pubrel` | `[qos2]` | PUBREC advances entry to `WaitingForPubcomp` and returns correct PUBREL. |
| `on_pubrec_duplicate_is_idempotent` | `[qos2]` | Duplicate PUBREC returns PUBREL without throwing. |
| `on_pubrec_unknown_id_throws` | `[qos2]` | PUBREC for unknown outbound packet_id throws `UnexpectedPacketId`. |
| `on_pubcomp_removes_entry` | `[qos2]` | PUBCOMP removes entry and releases Packet ID. |
| `on_pubcomp_unknown_id_throws` | `[qos2]` | PUBCOMP for unknown outbound packet_id throws `UnexpectedPacketId`. |
| `retransmit_before_pubrec_returns_publish_with_dup` | `[qos2]` | Retransmit in `WaitingForPubrec` phase returns PUBLISH with DUP=true. |
| `retransmit_before_pubcomp_returns_pubrel` | `[qos2]` | Retransmit in `WaitingForPubcomp` phase returns PUBREL. |
| `retransmit_updates_timestamp` | `[qos2]` | Entry timestamp is refreshed after retransmit. |
| `retransmit_unknown_id_throws` | `[qos2]` | `retransmit` for unknown packet_id throws `UnexpectedPacketId`. |
