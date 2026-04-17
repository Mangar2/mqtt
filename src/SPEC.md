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

## Entry point

| File       | Contents |
|------------|----------|
| `main.cpp` | Application entry point (broker startup) |
