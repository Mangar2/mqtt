# src — Source Overview

Top-level source directory for the MQTT 5.0 broker.
Details for each module live in the `SPEC.md` files within the respective subdirectory.

## Modules

| Directory      | Plan ref | Description |
|----------------|----------|-------------|
| `data_model/`  | 1        | Pure data structures — primitive types, reason codes, properties, packet structs, message, subscription, session models. Header-only, no external dependencies. |
| `codec/`       | 2        | Serialization / deserialization of all MQTT 5.0 wire packets. Depends on `data_model/`. |
| `topic/`       | 3        | Topic-name and topic-filter validation; system-topic detection. Depends on `data_model/`. |

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

## Entry point

| File       | Contents |
|------------|----------|
| `main.cpp` | Application entry point (broker startup) |
