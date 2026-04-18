# data_model — Module 1 Overview

Pure data structures for the MQTT 5.0 broker. No business logic, no I/O, no external dependencies.
All types live in the `mqtt` namespace.

## Sub-modules

| Directory        | Plan ref | Contents |
|------------------|----------|----------|
| `types/`         | 1.1      | Primitive MQTT wire types |
| `reason_code/`   | 1.2      | Reason code enum and classification |
| `property/`      | 1.3      | Property identifier enum and value/mapping types |
| `packet/`        | 1.4      | Packet struct definitions for all 15 MQTT packet types |
| `message/`       | 1.5      | Protocol-agnostic Message and WillMessage model |
| `subscription/`  | 1.6      | Subscription, SubscriptionOptions, SharedSubscription |
| `session/`       | 1.7      | SessionState, InflightEntry and supporting enums |

## Design rules

- Header-only: every type is defined in a `.h` file; no `.cpp` files in this module.
- `#pragma once` guards on every header.
- `constexpr` / `[[nodiscard]]` used wherever appropriate.
- No raw owning pointers; heap types use `std::vector` / `std::optional`.
- `operator==` provided on every struct via `= default`.

## Utility helpers

- `reason_code/reason_code.h` provides `qos_to_granted_reason(QoS)` for
	SUBACK granted QoS mapping.
- `packet/subscribe_packets.h` provides
	`subscription_identifier_from(const SubscribePacket&)` to parse the optional
	Subscription Identifier property.
