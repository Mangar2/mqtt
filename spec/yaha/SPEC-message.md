# Message

## Purpose

The Message is the universal data carrier of the YAHA home automation system. Every piece of information that flows between devices, services, and the broker is a Message. Nothing happens in the system without a Message.

## Role in the system

All YAHA components that implement `IMqttComponent` send and receive Messages. The Message type is the argument of `handleMessage` and the payload of every publish call. It is the shared contract that makes components interoperable — a component does not need to know who created a Message or who will receive it; it only needs to understand the Message format.

Normative format reference:
- [spec/@mangar2/mqtt-utils/src/message.ts](spec/@mangar2/mqtt-utils/src/message.ts)

## Fields

| Field   | Type               | Required | Default | Meaning                                                                 |
|---------|--------------------|----------|---------|-------------------------------------------------------------------------|
| topic   | string             | yes      | —       | MQTT topic path identifying the subject of the message                  |
| value   | string \| number   | yes      | `''`    | The current value or state being communicated                           |
| reason  | ReasonEntry[]      | no       | —       | Ordered list of reasons explaining why the value was set (trace chain)  |
| qos     | 0 \| 1 \| 2        | no       | 1       | MQTT quality of service level                                           |
| retain  | boolean            | no       | false   | Whether the broker should retain this message for new subscribers       |
| dup     | boolean            | no       | false   | Whether this is a re-delivery of a message the transport already sent   |

## Reason chain

The reason field is a list of `ReasonEntry` objects. Each entry records one step in the causal chain that led to this message being sent. As a message passes through the system, each component that forwards or transforms it may append its own reason entry.

### ReasonEntry

| Field     | Type   | Meaning                                          |
|-----------|--------|--------------------------------------------------|
| message   | string | Human-readable explanation for this step         |
| timestamp | string | ISO 8601 timestamp of when this reason was added |

The most recent reason is at the front of the list. The full list provides an end-to-end trace from the originating event to the final delivery.

## Semantics

### value

The value represents the current state of whatever the topic describes. It is intentionally untyped (string or number) to accommodate the wide variety of devices and sensors in a home automation system. Components that need typed values must parse or validate the value themselves.

### Convenience: isOn

A value is considered "on" if it equals `1`, `'on'`, `'ON'`, or `'true'`. This convention is used by components that control binary devices.

## Lifecycle

A Message is created by a component when it has new state to report (a sensor reading, a device status change, a rule firing). It travels via the MQTT broker to all subscribed components. Each receiving component gets the same Message. The Message is immutable in transit; if a component needs to modify it before forwarding, it creates a clone.

## Validation

A valid Message must have:
- `topic`: non-empty string
- `value`: string or number
- `reason`: array of ReasonEntry objects if present (not null, not a plain string)

Messages that fail validation must be rejected at the system boundary (e.g. when received from the broker). Internal components may assume they receive valid Messages.

## Canonical Transport Payload (MQTT)

For YAHA forwarded payloads, the canonical wire JSON is mandatory:

```json
{
	"message": {
		"topic": "<topic>",
		"value": "<string>|<number>",
		"reason": [
			{
				"timestamp": "<ISO-8601>",
				"message": "<reason text>"
			}
		]
	}
}
```

Rules:
- `message.topic` and `message.value` are required.
- `message.reason` is optional and omitted when undefined/empty.
- `reason` entries are serialized oldest-first (JS/TS reference behavior).
- `timestamp` and `message` field names are mandatory when a reason entry is present.
- Transport serialization must always emit this canonical envelope JSON format (no alternate scalar/raw payload on wire).

### Normative Semantics

- Field access semantics are identical across clients:
	- topic read/write uses `message.topic`
	- value read/write uses `message.value`
	- reason chain uses `message.reason` with `ReasonEntry` objects
- Reason chain order contract:
	- on wire: oldest-first
	- consumers must preserve order and must not reorder by timestamp
- Timestamp contract:
	- newly generated timestamps must be ISO 8601 UTC (`YYYY-MM-DDTHH:MM:SS(.sss)Z`)
	- parser accepts any string for backward compatibility, but new writes must follow ISO 8601 UTC
- Value contract:
	- wire value is JSON string or JSON number
	- booleans/null in payload are not part of canonical value type and must not be emitted by builders

### Compliance Requirement

All YAHA clients and adapters that build or parse YAHA message payloads must use the same canonical envelope rules above.
Any deviation is a format bug.

## Canonical Message-Flow Logging Contract

Message-flow logging (incoming/outgoing message logs) is a shared message-service capability and must use one unified contract across YAHA clients.

Mandatory shared behavior:
- message-flow logs must be produced via shared message logging services in `src/yaha/message/`.
- every log path must be able to emit the full reason chain (`ReasonEntry[]`), not a flattened single reason string.
- topic-based filtering must be centralized and direction-aware (`incoming`, `outgoing`).

### Required log fields

Every unified message-flow log line must include:
- `component`
- `direction`
- `topic`
- `value`
- `qos`
- `retain`
- `dup`
- `reason`

Optional fields (only when available):
- `raw`
- transport metadata fields

### Deterministic field order

Unified log formatting must serialize fields in this order:
1. `component`
2. `direction`
3. `topic`
4. `value`
5. `qos`
6. `retain`
7. `dup`
8. `reason`
9. optional fields (`raw`, transport metadata) in stable append order

### Escaping rules

String values in unified message logs must use deterministic JSON-compatible escaping:
- `\\` for backslash
- `\"` for quote
- `\n`, `\r`, `\t`, `\b`, `\f` for control escapes
- control bytes below `0x20` must be emitted as `\u00XX`

### Reason-chain rule

- unified logging must never truncate reason entries.
- unified logging must never collapse `ReasonEntry[]` into one plain reason string.
- reason order in logs must follow `Message.reason()` order.

### Filter extension point

Shared logging services must provide a central filter extension point.

Required baseline behavior:
- topic wildcard matching with MQTT semantics (`+`, `#`)
- separate incoming and outgoing filter checks
- example supported filter shape: `/a/+/+`

Prepared extension behavior (implementation may come later):
- include/exclude filter chains
- central filter registry
- reloadable filter policy

## Architectural notes

- The Message type is the one data format shared by all YAHA components. It must be defined once and referenced everywhere — no component defines its own message type.
- The reason chain is the primary observability mechanism in YAHA. Preserving it across component boundaries is a system-level requirement, not optional.
- In C++ the Message is a value type (struct or class with value semantics). Components pass it by const reference for reading and by value (or clone) when they need to modify or forward it.

## Open questions

- Should `value` be widened to include bool, or kept as string | number only?
- Should the reason chain have a maximum length to prevent unbounded growth in long causal chains?
- Should Message carry a message ID for deduplication (relevant for QoS 2 handling at the application level)?
