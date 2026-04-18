# outbound_queue — Outbound Message Queue (Module 20)

Thread-safe per-client message queue that decouples the publishing thread from
the receiving client's QoS state. The broker's message router pushes `Message`
objects into the queue; the owning client thread pops them for QoS processing
and delivery.

Depends on: `data_model/` (1).

---

## Responsibilities

- Provide a thread-safe FIFO queue of `Message` objects.
- Allow any thread to push messages (the broker's delivery callback).
- Allow the owning client thread to pop messages (non-blocking).
- Signal shutdown to unblock any waiting consumer.
- Enforce a configurable maximum queue depth (drop on overflow).

---

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `outbound_queue.h/.cpp` | 20.1 | `OutboundQueue` — thread-safe FIFO of `Message` with push/try_pop/stop. |

---

## Public API

### `OutboundQueue`

```cpp
class OutboundQueue {
public:
    static constexpr std::size_t k_default_max_depth = 1000U;

    explicit OutboundQueue(std::size_t max_depth = k_default_max_depth);

    [[nodiscard]] bool push(Message msg);
    [[nodiscard]] std::optional<Message> try_pop();
    void stop();

    [[nodiscard]] bool is_stopped() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
};
```

### `push(Message)`

- Thread-safe: may be called from any thread.
- Returns `true` on success; `false` when the queue is full or stopped (message dropped).
- Does not block.

### `try_pop() -> optional<Message>`

- Non-blocking: returns `std::nullopt` when the queue is empty.
- Intended for the client's own thread.

### `stop()`

- Marks the queue as stopped. Subsequent `push()` calls fail immediately.
- Safe to call from any thread. Idempotent.

### `is_stopped() const`

- Returns `true` after `stop()` has been called.

### `is_empty() const`

- Returns `true` when the queue holds no messages.

### `size() const`

- Returns the current number of queued messages.

---

## Broker Integration (20.2)

- `Broker::register_connection()` stores a `shared_ptr<OutboundQueue>` per
  client instead of a raw `SendFn` closure.
- The `MessageRouter` `DeliverFn` callback pushes `Message` into the target
  client's `OutboundQueue`.
- No QoS processing happens on the publishing thread; the message is queued
  as-is for the receiving client's thread to process later.

---

## Backpressure Policy (20.1.5)

When the queue reaches `max_depth`, `push()` drops the incoming message and
returns `false`. This is a simple drop-on-overflow policy suitable for MQTT
where slow consumers should not block publishers.
