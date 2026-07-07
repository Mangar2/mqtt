# message — Module 1.5

Protocol-agnostic message representations used internally by the broker.
These types decouple the routing and storage layer from wire-format packet structs.

## 1.5.1 Message

A normalized representation of a publishable message.
Used by the message router, retained message store, and offline queue.

```cpp
struct Message {
    Utf8String             topic;
    BinaryData             payload;
    QoS                    qos{QoS::AtMostOnce};
    bool                   retain{false};
    std::vector<Property>  properties;  // PUBLISH-level properties
};
```

**Differences from `PublishPacket`:**

| Field | PublishPacket | Message |
|-------|--------------|---------|
| `dup` | present (protocol artifact) | absent |
| `packet_id` | present (protocol artifact) | absent |
| `topic` | Utf8String | Utf8String |
| `payload` | BinaryData | BinaryData |
| `qos` | QoS | QoS |
| `retain` | bool | bool |
| `properties` | vector\<Property\> | vector\<Property\> |

## 1.5.2 WillMessage

Stores the client's Will declaration independently of the CONNECT packet.
Used by the Will Manager and Session Store.

```cpp
struct WillMessage {
    Message   message;
    uint32_t  delay_interval{0};  // Will Delay Interval in seconds
};
```

`delay_interval` corresponds to the `WillDelayInterval` property (0x18) extracted from the
Will Properties block in the CONNECT packet. It controls how long the broker waits after
a connection loss before publishing the will. All other Will Properties (e.g.
`PayloadFormatIndicator`, `MessageExpiryInterval`) are stored inside `message.properties`
because they travel with the PUBLISH when the will is eventually sent.

## Design rules

- Header-only; defined in `message.h`.
- All types in the `mqtt` namespace.
- `operator==` defaulted on every struct.
