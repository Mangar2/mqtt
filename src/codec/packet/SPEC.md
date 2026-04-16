# packet — Module 2.4–2.12: Packet Codecs

Encodes and decodes the variable header and payload of all MQTT 5.0 control
packets. Builds on the fixed-header codec (2.3), the properties codec (2.2),
and the primitive codec (2.1).

## Files

| File                   | Plan refs | Contents |
|------------------------|-----------|------------------------------|
| `connect_codec.h/.cpp` | 2.4, 2.5  | CONNECT / CONNACK            |
| `publish_codec.h/.cpp` | 2.6, 2.7  | PUBLISH / PUBACK / PUBREC / PUBREL / PUBCOMP |
| `subscribe_codec.h/.cpp` | 2.8, 2.9 | SUBSCRIBE / SUBACK / UNSUBSCRIBE / UNSUBACK |
| `control_codec.h/.cpp` | 2.10–2.12 | PINGREQ / PINGRESP / DISCONNECT / AUTH |

## Codec contract

### Encode

All `encode_xxx(WriteBuffer& buf, const XxxPacket& p)` functions append the
**complete wire packet** (Fixed Header + Variable Header + Payload) to `buf`.

### Decode

All `decode_xxx(ReadBuffer& buf …)` functions receive a `ReadBuffer` that is
**bounded to exactly `remaining_length` bytes** (i.e. the span of bytes after
the Fixed Header). They consume all or part of those bytes and return the
decoded struct. The caller (`PacketReader`, module 2.13) is responsible for
slicing the buffer to `remaining_length` bytes before dispatching here.

Exceptions to the base signature:
- `decode_publish(ReadBuffer& buf, uint8_t flags)` — PUBLISH flags (DUP / QoS
  / RETAIN) are encoded in the Fixed Header and must be passed explicitly.
- `encode_pingreq` / `encode_pingresp` take no packet struct (empty packets).

## Wire-format reference

### CONNECT (2.4)

```
Variable header:
  Protocol Name  : UTF-8 string "MQTT"   (0x00 0x04 0x4D 0x51 0x54 0x54)
  Protocol Level : byte 0x05
  Connect Flags  : 1 byte
    bit 7 : UserName Flag
    bit 6 : Password Flag
    bit 5 : Will Retain
    bit 4-3: Will QoS
    bit 2 : Will Flag
    bit 1 : Clean Start
    bit 0 : Reserved (must be 0)
  Keep Alive     : 2-byte big-endian
  Properties     : VBI length + properties (context = Connect)

Payload:
  ClientID       : UTF-8 string
  [if Will Flag]
    Will Prop.   : VBI length + properties (context = Will)
    Will Topic   : UTF-8 string
    Will Payload : binary data
  [if UserName Flag]
    Username     : UTF-8 string
  [if Password Flag]
    Password     : binary data
```

### CONNACK (2.5)

```
Variable header:
  Ack Flags      : byte (bit 0 = session_present; bits 7-1 reserved = 0)
  Reason Code    : byte
  Properties     : VBI length + properties (context = Connack)
```

### PUBLISH (2.6)

```
Fixed-header flags: [DUP=bit3] [QoS=bits2-1] [RETAIN=bit0]

Variable header:
  Topic Name     : UTF-8 string
  [QoS > 0]
    Packet ID    : 2-byte big-endian
  Properties     : VBI length + properties (context = Publish)

Payload:
  Payload bytes  : all remaining bytes in the bounded buffer
```

### PUBACK / PUBREC / PUBREL / PUBCOMP (2.7)

```
Variable header:
  Packet ID      : 2-byte big-endian
  [remaining_length > 2]
    Reason Code  : byte
    Properties   : VBI length + properties (context = Puback/Pubrec/Pubrel/Pubcomp)

Short form: remaining_length == 2 → Reason Code = 0x00, Properties = empty.
PUBREL Fixed-header flags must be 0x02.
```

### SUBSCRIBE (2.8)

```
Variable header:
  Packet ID      : 2-byte big-endian
  Properties     : VBI length + properties (context = Subscribe)

Payload (one or more entries):
  Topic Filter   : UTF-8 string
  Options        : byte
    bits 1-0     : Maximum QoS (0/1/2; 3 is reserved → error)
    bit  2       : No Local
    bit  3       : Retain As Published
    bits 5-4     : Retain Handling (0/1/2; others → error)
    bits 7-6     : Reserved (must be 0)
```

At least one filter is required; zero filters is a protocol error.

### SUBACK (2.9)

```
Variable header:
  Packet ID      : 2-byte big-endian
  Properties     : VBI length + properties (context = Suback)

Payload:
  Reason codes   : one byte per subscribed filter
```

### UNSUBSCRIBE (2.9)

```
Variable header:
  Packet ID      : 2-byte big-endian
  Properties     : VBI length + properties (context = Unsubscribe)

Payload:
  Topic filters  : one UTF-8 string per filter (at least one required)
```

### UNSUBACK (2.9)

```
Variable header:
  Packet ID      : 2-byte big-endian
  Properties     : VBI length + properties (context = Unsuback)

Payload:
  Reason codes   : one byte per unsubscribed filter
```

### PINGREQ / PINGRESP (2.10)

No variable header, no payload. `remaining_length` must be 0.

### DISCONNECT (2.11)

```
Variable header:
  [remaining_length > 0]
    Reason Code  : byte
  [remaining_length > 1]
    Properties   : VBI length + properties (context = Disconnect)

Short form: remaining_length == 0 → Reason Code = 0x00 (Normal Disconnection).
```

### AUTH (2.12)

```
Variable header:
  [remaining_length > 0]
    Reason Code  : byte (valid: 0x00 Success, 0x18 Continue, 0x19 Re-authenticate)
  [remaining_length > 1]
    Properties   : VBI length + properties (context = Auth)

Short form: remaining_length == 0 → Reason Code = 0x00 (Success).
```

## Error codes used

| CodecError               | When |
|--------------------------|------|
| `BufferTooShort`         | input truncated |
| `InvalidProtocolName`    | CONNECT: protocol name ≠ "MQTT" |
| `InvalidProtocolVersion` | CONNECT: protocol level ≠ 5 |
| `InvalidQoS`             | QoS bits = 3 (reserved) |
| `MalformedPacket`        | reserved bits set, conflicting flags, invalid retain_handling, bad AUTH reason code, zero SUBSCRIBE/UNSUBSCRIBE filters |
| `StringTooLong`          | UTF-8 string or binary exceeds 65 535 bytes |
| `PropertyNotAllowed`     | from properties codec |
| `DuplicateProperty`      | from properties codec |
| `VariableByteIntegerOverflow` | from primitive codec |
