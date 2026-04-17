# src — Source Overview

Top-level source directory for the MQTT 5.0 broker.
Details for each module live in the `SPEC.md` files within the respective subdirectory.

## Modules

| Directory      | Plan ref | Description |
|----------------|----------|-------------|
| `data_model/`  | 1        | Pure data structures — primitive types, reason codes, properties, packet structs, message, subscription, session models. Header-only, no external dependencies. |
| `codec/`       | 2        | Serialization / deserialization of all MQTT 5.0 wire packets. Depends on `data_model/`. |
| `topic/`       | 3        | Topic-name and topic-filter validation, subscription trie storage, and topic matching. Depends on `data_model/`. |
| `store/`       | 4        | In-memory runtime state: subscription store, retained message store, session store, and inflight store. Depends on `data_model/` and `topic/`. |
| `qos/`         | 5        | QoS Engine — Packet ID allocation, QoS 1 and QoS 2 state machines with retransmission. Depends on `data_model/` and `store/`. |
| `network/`     | 6        | Network Layer — TCP listener, connection wrapper, incoming stream buffer, and outgoing write queue. No MQTT knowledge. No external dependencies. |
| `connection/`  | 7        | Connection Handler — lifecycle state machine, keep-alive timer, topic alias table, and receive-maximum flow controller for a single client connection. |

## `data_model/` sub-modules

| Directory              | Plan ref | Contents |
|------------------------|----------|----------|
| `data_model/types/`       | 1.1      | Primitive MQTT wire types (VarByteInt, UTF-8 string, binary data, integers) |
| `data_model/reason_code/` | 1.2      | Reason code enum (39 values) and success/error classification |
| `data_model/property/`    | 1.3      | Property identifier enum and value/mapping types |
| `data_model/packet/`      | 1.4      | Struct definitions for all 15 MQTT packet types |
| `data_model/message/`     | 1.5      | Message and WillMessage model |
| `data_model/subscription/`| 1.6      | Subscription, SubscriptionOptions, SharedSubscription |
| `data_model/session/`     | 1.7      | SessionState, InflightEntry and supporting enums |

## `codec/` sub-modules

| Directory / File           | Plan ref  | Contents |
|----------------------------|-----------|----------|
| `codec/primitive/`         | 2.1       | Encode / decode of all MQTT primitive wire types |
| `codec/properties/`        | 2.2       | Properties section encoder, decoder, validator |
| `codec/fixed_header/`      | 2.3       | Fixed-header encoder and decoder |
| `codec/packet/`            | 2.4–2.12  | Variable-header + payload codecs for all 15 control packets |
| `codec/codec_error.h`      | —         | `CodecError` enum and `CodecException` |
| `codec/read_buffer.h`      | —         | `ReadBuffer` — read-only cursor over a byte span |
| `codec/write_buffer.h`     | —         | `WriteBuffer` — alias for `std::vector<uint8_t>` |

## `topic/` sub-modules

| Directory / File                     | Plan ref | Contents |
|--------------------------------------|----------|----------|
| `topic/topic_error.h`                | 3.1      | `TopicError` enum and `TopicException` |
| `topic/topic_validator.h/.cpp`       | 3.1      | `validate_topic_name`, `validate_topic_filter`, `is_system_topic` |
| `topic/subscription_trie.h/.cpp`     | 3.2      | `SubscriptionTrie` — trie storage for MQTT subscriptions (insert, remove, remove_all) |
| `topic/topic_matcher.h/.cpp`         | 3.3      | `TopicMatcher` — matches a publish topic name against all trie subscriptions (exact, `+`, `#`, system-topic exclusion) |

## `store/` sub-modules

| Directory / File                              | Plan ref | Contents |
|-----------------------------------------------|----------|----------|
| `store/store_error.h`                          | 4        | `StoreError` enum and `StoreException` |
| `store/subscription_store.h/.cpp`              | 4.1      | In-memory subscription store; wraps `SubscriptionTrie` + `TopicMatcher` |
| `store/retained_message_store.h/.cpp`          | 4.2      | In-memory map of retained messages keyed by topic name |
| `store/session_store.h/.cpp`                   | 4.3      | In-memory map of `SessionState` records keyed by client ID |
| `store/inflight_store.h/.cpp`                  | 4.4      | Per-session `InflightEntry` arrays; tracks in-use packet IDs |

## `qos/` sub-modules

| Directory / File                              | Plan ref | Contents |
|-----------------------------------------------|----------|----------|
| `qos/qos_error.h`                              | 5        | `QosError` enum and `QosException` |
| `qos/packet_id_manager.h/.cpp`                 | 5.1      | Per-session Packet Identifier allocator; separate inbound/outbound spaces |
| `qos/qos1_state_machine.h/.cpp`                | 5.2      | QoS 1 (AtLeastOnce) inbound/outbound handshake and retransmission logic |
| `qos/qos2_state_machine.h/.cpp`                | 5.3      | QoS 2 (ExactlyOnce) inbound/outbound handshake, duplicate detection, retransmission |

## `network/` sub-modules

| Directory / File                              | Plan ref | Contents |
|-----------------------------------------------|----------|----------|
| `network/network_error.h`                      | 6        | `NetworkError` enum and `NetworkException` |
| `network/tcp_connection.h/.cpp`                | 6.1.3    | `TcpConnection` — owns a connected socket fd; blocking read/write/close |
| `network/tcp_listener.h/.cpp`                  | 6.1.1–2  | `TcpListener` — opens server socket, blocks on accept, hands off connections |
| `network/stream_buffer.h/.cpp`                 | 6.2      | `StreamBuffer` — accumulates TCP bytes, extracts complete MQTT packets via VBI framing |
| `network/write_queue.h/.cpp`                   | 6.3      | `WriteQueue` — thread-safe outgoing packet queue; async drain with backpressure |

## `connection/` sub-modules

| Directory / File | Plan ref | Contents |
|------------------|----------|----------|
| `connection/connection_error.h` | 7 | `ConnectionError` enum and `ConnectionException` |
| `connection/connection_state.h/.cpp` | 7.1 | `ConnectionStateMachine` — lifecycle states Connecting → Connected → Disconnecting → Closed |
| `connection/keep_alive_timer.h/.cpp` | 7.2 | `KeepAliveTimer` — 1.5 × Keep Alive deadline with reset-on-packet |
| `connection/topic_alias_table.h/.cpp` | 7.3 | `TopicAliasTable` — inbound and outbound alias↔topic mappings with maximum enforcement |
| `connection/receive_maximum.h/.cpp` | 7.4 | `ReceiveMaximum` — inflight QoS 1/2 packet counter with pause/resume flow control |

## Entry point

| File       | Contents |
|------------|----------|
| `main.cpp` | Application entry point (broker startup) |
