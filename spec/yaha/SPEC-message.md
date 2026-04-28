# Message

## Purpose

The Message is the universal data carrier of the YAHA home automation system. Every piece of information that flows between devices, services, and the broker is a Message. Nothing happens in the system without a Message.

## Role in the system

All YAHA components that implement `IMqttComponent` send and receive Messages. The Message type is the argument of `handleMessage` and the payload of every publish call. It is the shared contract that makes components interoperable — a component does not need to know who created a Message or who will receive it; it only needs to understand the Message format.

## Fields

| Field   | Type               | Required | Default | Meaning                                                                 |
|---------|--------------------|----------|---------|-------------------------------------------------------------------------|
| topic   | string             | yes      | —       | MQTT topic path identifying the subject of the message                  |
| value   | string \| number   | yes      | `''`    | The current value or state being communicated                           |
| reason  | ReasonEntry[]      | no       | —       | Ordered list of reasons explaining why the value was set (trace chain)  |
| qos     | 0 \| 1 \| 2        | no       | 1       | MQTT quality of service level                                           |
| retain  | boolean            | no       | false   | Whether the broker should retain this message for new subscribers       |

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

## Architectural notes

- The Message type is the one data format shared by all YAHA components. It must be defined once and referenced everywhere — no component defines its own message type.
- The reason chain is the primary observability mechanism in YAHA. Preserving it across component boundaries is a system-level requirement, not optional.
- In C++ the Message is a value type (struct or class with value semantics). Components pass it by const reference for reading and by value (or clone) when they need to modify or forward it.

## Open questions

- Should `value` be widened to include bool, or kept as string | number only?
- Should the reason chain have a maximum length to prevent unbounded growth in long causal chains?
- Should Message carry a message ID for deduplication (relevant for QoS 2 handling at the application level)?
