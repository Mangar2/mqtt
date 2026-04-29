# mqtt_component test specification

## Scope

Unit tests for the `IMqttComponent` interface contract.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `DefaultSetPublishCallback_NoThrow` | Component does not override callback setter | Non-null callback | No exception |
| `PolymorphicHandleMessage_DispatchesToDerived` | Base reference calls derived implementation | One message | Derived records the message |
| `GetSubscriptions_ReturnsTopicToQosMap` | Derived component exposes subscriptions | None | Map contains expected topics/QoS |
| `OverriddenSetPublishCallback_CanPublishOutward` | Publisher component stores callback and emits message | Callback + emitted message | Callback invoked with emitted message |
