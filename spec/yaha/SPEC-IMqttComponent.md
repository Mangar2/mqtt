# IMqttComponent — Component Interface

## Purpose

Defines the contract between an MQTT client and a home automation component. Allows any component to be connected to an MQTT client without either side knowing about the other's internals. The interface is the only coupling point.

## Role in the system

Every YAHA component that needs to receive or send MQTT messages implements this interface. The MQTT client calls `getSubscriptions` once at startup to know what to subscribe to, and calls `handleMessage` for every incoming message that matches a subscription. If a component needs to publish messages itself, it uses a callback or injected publish function that is also provided through this interface boundary.

A `main` entry point creates an MQTT client and a component independently, wires them together via the interface, and starts both. Neither the MQTT client source nor the component source contains any reference to the other.

## Interface contract

### getSubscriptions

Returns the set of MQTT topic patterns this component wants to receive, together with the required QoS level for each. Called once by the MQTT client after the connection is established and again after a reconnect.

**Result shape:** a mapping from topic pattern (string) to QoS level (0, 1, or 2).

### handleMessage

Called by the MQTT client for every message whose topic matches a subscription returned by `getSubscriptions`. Delivers the full message including topic, payload, and metadata. The component processes the message synchronously or schedules its own async work; the interface itself is fire-and-forget from the client's perspective.

**Parameters:** a message object containing at minimum topic, payload, QoS, and retain flag.

### setPublishCallback *(optional)*

If a component needs to publish messages, the MQTT client injects a publish function through this method before the first message is delivered. The component stores this function and calls it when it needs to send a message to the broker. The component must not assume the function is available before it has been injected.

## Behavioral requirements

- The MQTT client must call `getSubscriptions` after every successful (re)connect and subscribe to the returned patterns.
- The MQTT client must not pass a message to `handleMessage` for topics that are not covered by the current subscriptions.
- A component must not depend on call order beyond: `getSubscriptions` before `handleMessage`, `setPublishCallback` before any outgoing publish attempt.
- Components that do not publish outgoing messages need not implement `setPublishCallback`.

## Composition in main

The main entry point:
1. Creates the component (passing its configuration).
2. Creates the MQTT client (passing its configuration).
3. Wires them: passes the component as the message handler to the MQTT client.
4. Starts the MQTT client, which manages the connection lifecycle.

The component does not start the MQTT client. The MQTT client does not construct the component.

## Architectural notes

- In C++ this interface is a pure abstract class (`IMqttComponent`). No virtual destructor implementation is required in the interface itself but must be declared.
- The MQTT client holds a reference or pointer to `IMqttComponent` — it does not own the component.
- Thread safety of `handleMessage` across reconnect events is a concern for the implementation, not this interface.

## Open questions

- Should `handleMessage` return a result (e.g. ACK/error) or remain void? The legacy code is fire-and-forget; QoS acknowledgement is handled at the transport layer.
- Should a component be allowed to register itself for multiple message-type categories, or is one `handleMessage` entry point sufficient?
