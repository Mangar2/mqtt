# packet — Module 1.4: Packet Structures

Struct definitions for all 15 MQTT 5.0 control packet types (Section 3 of the spec).
Each struct models the variable header and payload fields only; the fixed header is
handled by the codec module (2.3).

## Files

| File                  | Packet types |
|-----------------------|--------------|
| `packet_type.h`       | `PacketType` enum (1–15 + internal `Will`) |
| `connect_packet.h`    | `WillData`, `ConnectPacket` (1.4.1), `ConnackPacket` (1.4.2) |
| `publish_packets.h`   | `PublishPacket` (1.4.3), `PubackPacket` (1.4.4), `PubrecPacket` (1.4.5), `PubrelPacket` (1.4.6), `PubcompPacket` (1.4.7) |
| `subscribe_packets.h` | `SubscribeOptions`, `SubscribeFilter`, `SubscribePacket` (1.4.8), `SubackPacket` (1.4.9), `UnsubscribePacket` (1.4.10), `UnsubackPacket` (1.4.11) |
| `control_packets.h`   | `PingreqPacket` (1.4.12), `PingrespPacket` (1.4.13), `DisconnectPacket` (1.4.14), `AuthPacket` (1.4.15) |

## Common conventions

- Properties are stored as `std::vector<Property>`.
- All packet structs provide `operator==` defaulted.
- Optional fields use `std::optional<T>`.
- Packet IDs are `uint16_t`; present only where the spec requires them.
- `WillData` represents the Will topic, payload, QoS, retain flag, and properties
  embedded within the CONNECT packet payload.
