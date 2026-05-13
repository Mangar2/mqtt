# mqtt_component — IMqttComponent Interface

## Purpose

Defines the transport-agnostic interface between YAHA components and the MQTT session layer.
Any component that consumes MQTT messages implements this interface.

## Public API

### Type aliases

```cpp
using SubscriptionMap = std::map<std::string, Qos>;
enum class PublishFailureCategory : std::uint8_t;
struct PublishResult;
class PublishCallback; // wraps void and PublishResult callables
```

`PublishResult` contains explicit delivery outcome:
- `success` boolean
- failure `category` (`Disconnected`, `AckTimeout`, `WriteFailed`, `CallbackMissing`, `Unknown`)
- optional `reason` text

`PublishCallback` wraps both callable shapes:
- `void(const Message&)` (treated as success when no throw)
- `PublishResult(const Message&)`

### Class IMqttComponent

| Member | Signature | Notes |
|--------|-----------|-------|
| Destructor | `virtual ~IMqttComponent()` | virtual and defaulted in `.cpp` |
| `getSubscriptions()` | `virtual SubscriptionMap () const = 0` | called after every (re)connect |
| `handleMessage()` | `virtual void (const Message&) = 0` | fire-and-forget delivery |
| `setPublishCallback()` | `virtual void (PublishCallback)` | optional for components that publish; callback returns explicit delivery result |

## Behavioral constraints

- MQTT client must call `getSubscriptions()` after each successful connect and reconnect.
- MQTT client calls `handleMessage()` only for subscribed topics.
- Components that do not publish can ignore `setPublishCallback()`.
- Interface owns no transport resources and no threads.

## Files

| File | Role |
|------|------|
| `mqtt_component.h` | Interface declarations |
| `mqtt_component.cpp` | Default method implementations |
| `test/mqtt_component_test.cpp` | Unit tests |
| `test/TEST_SPEC.md` | Test specification |
