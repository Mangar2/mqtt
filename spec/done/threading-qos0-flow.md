# Threading Flow — QoS 0 Message End-to-End

Describes the complete runtime path of a single QoS 0 PUBLISH from the
publishing client (Client A) to the subscribing client (Client B).
Starting state: both clients are already connected and Client B has an
active subscription matching the published topic.

---

## Involved Objects

| Object | Module | Role |
|--------|--------|------|
| `IoReactor` | `network/` | Single-thread event loop (kqueue / epoll). Waits on kernel for socket readability / writability. Never blocks on MQTT logic. |
| `ConnectionSlot` | `network/` | Per-fd struct: socket handle + ring-buffer for outgoing bytes. No lock — serialized by `JobScheduler`. |
| `ConnectionSession` | `connection/` | Per-connection heap object: `StreamBuffer` (inbound bytes), `ClientSession`, pending write frames, phase flag. No lock — same serialization guarantee. |
| `StreamBuffer` | `network/` | Accumulates raw TCP bytes; reports when a complete MQTT packet is available. |
| `ClientSession` | `client_session/` | Per-client state machine: `PacketIdManager`, QoS state machines, `KeepAliveTimer`, `TopicAliasTable`, shared `OutboundQueue`. **Not** thread-safe; only ever touched by one worker at a time. |
| `OutboundQueue` | `outbound_queue/` | Thread-safe FIFO of `Message` objects. Written by the publishing worker thread; read by the subscribing client's drain worker thread. This is the only explicit cross-thread handoff point for a message. |
| `JobScheduler` | `executor/` | Per-fd serialization map. Holds a backlog of jobs per fd; at most one job per fd is active at any moment. |
| `WorkerPool` | `executor/` | Elastic pool of N worker threads, all sleeping on `JobQueue::pop_blocking()`. Woken individually when a job is enqueued. |
| `JobQueue` | `executor/` | Concurrent FIFO protected by mutex + condition variable. Workers block here when idle. |
| `PublishFacade` | `broker/` | Routes an inbound `Message` through `MessageRouter`. Has its own internal lock. |
| `MessageRouter` | `message_router/` | Matches topic to subscriptions, builds `DeliveryItem` list, calls per-client `deliver_function` callbacks. |
| `SubscriberFanout` | `message_router/` | Stateless: applies No Local, QoS downgrade, Subscription Identifier per subscriber. |
| `ActiveConnectionRegistry` | `broker/` | Maps `client_id → fd`. Read by `Broker::wake_outbound()` to find the drain target fd. |
| `Broker` | `broker/` | Thin orchestrator; provides `wake_outbound(client_id)` which submits a `DrainJob` for the target fd. |
| `ConnectionTable` | `network/` | fd-indexed table of `{ ConnectionSlot, ConnectionSession }` entries. Protected by shared_mutex on insert/remove; find is read-locked. |
| `KeepaliveWatchdog` thread | `connection/` | Separate thread inside `ConnectionManager`; polls the connection table at most every 100 ms to fire timed `DecodeJob`s for keep-alive / retransmit deadlines. |

---

## Involved Threads

| Thread | Count | Activation |
|--------|-------|------------|
| **IoReactor thread** | 1 | Permanently blocked in `kevent` / `epoll_wait`. Only wakes when the OS reports an event. Never polls. |
| **Worker threads** | N (elastic, min configured) | Sleep on `JobQueue::pop_blocking()` (condition variable). Wake only when a job is pushed. Never poll. |
| **KeepaliveWatchdog thread** | 1 | Sleeps on a condition variable with a calculated timeout (next deadline or max 100 ms). Only submits work when a deadline fires. Polls the connection table at the end of each sleep, but only to recalculate the next wake-up — no MQTT processing happens here. |

---

## Step-by-Step Flow

### Phase 1 — Inbound: Client A sends PUBLISH to the broker

```
Client A TCP socket
     │
     │ (1) TCP data arrives on Client A's socket fd
     ▼
IoReactor thread
  • kevent/epoll_wait returns fd = fdA, read-ready
  • calls registered ReadCallback for fdA
  • ReadCallback: JobScheduler.submit( DecodeJob{fd=fdA} )
     │
     │  JobScheduler checks: no active job for fdA → enqueues to JobQueue directly
     ▼
JobQueue (mutex + cv)
  • push() acquires mutex, appends job, signals condition variable
     │
     │  (2) one idle Worker wakes via condition variable
     ▼
Worker thread W1  (was sleeping in pop_blocking())
  • pops DecodeJob{fd=fdA}
  • calls process_decode_job(fdA, ...)

  process_decode_job:
    • nb_read(fdA)  →  non-blocking read, fills read_chunk[]
    • session.stream_buffer().append(bytes)   ← raw bytes into StreamBuffer
    • loop: decode_one_packet(session, broker)
        • StreamBuffer extracts complete MQTT fixed+variable header + payload
        • PublishPacket decoded
        • runtime_step: session.client_session()->on_publish(publish_packet)
            • QoS 0: no ACK frame generated
            • returns InboundPublishResult{ message }
        • broker.handle_publish(message, client_id, username, alias_table)
            ↓
          PublishFacade::handle_publish()   [no lock in PublishFacade itself;
            thread-safety comes from locks inside MessageRouter / SubscriptionStore]
            ↓
          MessageRouter::route(message)
            ↓
          InboundPublishProcessor::process()
            • ACL check (publish authorised)
            • retain handling (QoS 0 → no store for this message)
            • SubscriptionStore::match(topic) → [ { client_id="B", qos=0, ... } ]
            ↓
          SubscriberFanout::prepare(message, subscribers, publisher_id)
            • No Local check
            • QoS cap: delivery_qos = min(publish_qos, subscription_qos)
              in this scenario both are 0, so trivially 0 — no actual downgrade
            • builds DeliveryItem list
            ↓
          for each DeliveryItem:
            MessageRouter::dispatch_item("B", message)
              • MessageExpiryController::update_expiry() — already expired → discard
              • is_online_("B") check (reads ActiveConnectionRegistry, internally locked)
                  → true: call deliver_("B", message)         ← see deliver_function below
                  → false + QoS 0: silent return (no offline queue for QoS 0)
                  → false + QoS 1/2: offline_queue_.enqueue()

          deliver_function("B", message)   ← lambda constructed in BrokerModuleFactory
              • estimated_publish_frame_bytes(message)
                  NOTE: this fully encodes the PUBLISH packet just to measure byte size.
                  At high publish rates this is a measurable per-message cost.
              • if frame_bytes > write_queue_max_bytes → throw QueueFull (message too large)
              • statistics_collector.on_message_outbound()
              • OutboundQueue_B.push(message)   ← internal mutex; non-blocking
                  → push returns true (queue has room):
                      wake_outbound_callback("B")
                        Broker::wake_outbound("B")
                          ActiveConnectionRegistry::fd_for("B") → fdB
                          JobScheduler::submit( DrainJob{fd=fdB} )
                  → push returns false (queue full, max_queued_messages reached):
                        QoS 0: silent return — no exception, no wake_outbound
                        QoS 1/2: throw MessageRouterException(QueueFull)
                                 → PublishFacade catches → returns QuotaExceeded
                                 → runtime_step encodes error PUBACK/PUBREC

    • after decode loop: drain_outbound_to_write_buffer(session, broker)
        • client_session->drain_outbound() — no outbound messages for Client A in QoS 0
    • slot.write_size() == 0 → no write arm needed
    • JobScheduler::mark_done(fdA)
```

**Thread activation summary for Phase 1:**
- IoReactor wakes on kernel event (not polling).
- Worker W1 wakes on condition variable signal from `JobQueue::push()`.
- Worker W1 submits `DrainJob{fdB}` into `JobQueue` before it exits — this is the trigger for Phase 2.
- Note: `runtime_step` does NOT check the return code of `handle_publish` for QoS 0.
  A `QuotaExceeded` or `NoMatchingSubscribers` result is silently discarded for QoS 0 —
  there is no PUBACK to encode and no error to signal to the publisher.

---

### Phase 2 — Outbound: broker delivers message to Client B

```
JobQueue
  • DrainJob{fd=fdB} was pushed by wake_outbound() above
     │
     │  (3) one idle Worker wakes (may be same W1 or another Wn)
     ▼
Worker thread Wn
  • pops DrainJob{fd=fdB}
  • calls process_drain_job(fdB, ...)

  process_drain_job:
    • drain_outbound_to_write_buffer(session_B, broker)
        • client_session_B->drain_outbound()
            • OutboundQueue::try_pop()  → Message (the QoS 0 publish)
            • QoS 0: directly encode PublishPacket → WriteBuffer
            • returns [ WriteBuffer ]
        • session_B.pending_write_frames().push_back( frame )
    • move_pending_frames_to_slot(session_B, slot_B)
        • frame copied into ConnectionSlot_B ring-buffer
    • drain_socket_write_buffer(slot_B, budget)
        • nb_write(fdB, bytes)  →  non-blocking write to Client B's socket
        • if all bytes sent: slot_B.write_size() == 0
    • reactor.disarm_write(fdB)   ← no more write interest
    • JobScheduler::mark_done(fdB)
```

**Thread activation summary for Phase 2:**
- Worker Wn wakes on condition variable signal from `JobQueue::push()` (triggered by `wake_outbound`).
- No polling. No timer. The worker is woken exactly because the message arrived.

> **Reactor write-arm path (alternative when socket is not immediately writable):**
> If `nb_write` returns `WouldBlock` (socket send-buffer full), the bytes remain in
> `ConnectionSlot_B.write_buffer` and `reactor.arm_write(fdB)` is called.  
> When the OS reports `fdB` write-ready, the IoReactor fires the registered
> `WriteCallback`, which submits a new `DrainJob{fdB}`.  
> A worker then picks it up and retries `nb_write` — no polling, fully event-driven.

---

## Full Thread Interaction Diagram

```
   IoReactor        Worker W1           Worker Wn        KeepaliveWatchdog
   (1 thread)       (from pool)         (from pool)       (1 thread)
       │                 │                   │                  │
  [epoll_wait]       [pop_blocking]      [pop_blocking]    [cv_wait_until]
       │                 │                   │                  │
  fdA read-ready         │                   │                  │
       │                 │                   │                  │
  ReadCallback ──► submit(DecodeJob{fdA})    │                  │
       │                 │                   │                  │
  [epoll_wait]    wake via cv               │                  │
                         │                   │                  │
                  process_decode_job         │                  │
                    nb_read(fdA)             │                  │
                    decode PUBLISH           │                  │
                    PublishFacade            │                  │
                    MessageRouter            │                  │
                    OutboundQueue_B.push() ──┤                  │
                    wake_outbound("B") ──► submit(DrainJob{fdB})│
                         │                   │                  │
                  mark_done(fdA)       wake via cv             │
                         │                   │                  │
                  [pop_blocking]      process_drain_job         │
                                        drain_outbound()        │
                                        encode PUBLISH          │
                                        nb_write(fdB)           │
                                      mark_done(fdB)            │
                                        │                       │
                                   [pop_blocking]          [cv_wait_until]
```

---

## What Happens When Client B Disconnects After the Message Was Enqueued

Scenario: `OutboundQueue_B.push()` succeeded (message is in the queue) but
before Worker Wn calls `nb_write(fdB)`, Client B's TCP connection drops.

### Detection paths

There are two ways the broker learns about the disconnect:

**Path A — IoReactor detects TCP FIN/RST (most common):**
1. OS signals fdB as read-ready (EOF) or error.
2. IoReactor fires `ReadCallback` for fdB → `submit(DecodeJob{fdB})`.
3. Worker picks up `DecodeJob{fdB}`, calls `nb_read(fdB)` → `IoResult::Closed` (0 bytes or error).
4. `peer_closed = true`; after stream buffer is drained → `close_after_flush = true`.
5. If write buffer has data: `arm_write(fdB)`, submit `DrainJob{fdB}`.
   If write buffer is empty: `submit(CloseJob{fdB})` directly.

**Path B — Worker sees `nb_write` failure during Drain:**
1. Worker Wn executes `process_drain_job(fdB, ...)`.
2. `nb_write(fdB)` returns error (socket already gone) → `write_drain_result.success = false`.
3. `process_drain_job` calls `process_close_job(fdB, ...)` inline.

### Close finalization (`process_close_job`)

In beiden Paths läuft der Close-Job auf einem Worker-Thread:

```
process_close_job(fdB):
  • close_step::finalize_close(session_B, broker)
      • session_B.disconnect_state().clean_disconnect == false  (unexpected disconnect)
      • broker.handle_connection_lost("B", now, outbound_queue_B)
          ↓
        DisconnectFacade::handle_connection_lost()
          • OutboundQueue_B.stop()   ← all future push() calls return false immediately
          • unregister_connection("B")
              • ActiveConnectionRegistry::remove("B")
                → is_online_("B") returns false from this point
                → MessageRouter::dispatch_item now takes the offline path for "B"
                → QoS 0 messages to "B" are silently dropped in dispatch_item
                   without even calling deliver_() — no OutboundQueue access at all
              • OutboundQueue shared_ptr erased → queue eventually destroyed
          • session_manager.on_connection_lost("B")
              • SessionStore: mark session as disconnected (persists if clean_session=false)
          • will_manager: schedule / fire Will Message if configured
  • reactor.unregister(fdB)
  • table.remove(fdB)   ← ConnectionSlot + ConnectionSession destroyed
  • TcpConnection::close(fdB)
```

### Fate of the queued message

| Stage | What happens to the message |
|-------|-----------------------------|
| Message in `OutboundQueue_B`, not yet popped | `OutboundQueue_B.stop()` is called; subsequent `push()` fails. The message already in the queue is simply never popped — it is destroyed with the queue. QoS 0 semantics: at-most-once delivery, no retransmission. |
| Message encoded in `ConnectionSlot_B.write_buffer` | `nb_write` failed. Buffer is discarded when `ConnectionSlot` is destroyed inside `table.remove(fdB)`. |
| `process_drain_job` is still active when `process_close_job` runs | The `JobScheduler` serialization prevents this: only one job per fd is active at a time. If a `DrainJob` is active, `CloseJob` is held in the per-fd backlog and runs only after `mark_done(fdB)` completes the drain. In Path B the drain itself calls `process_close_job` directly while still being the active job. |

### Key property for QoS 0

At-most-once delivery means **no error is signalled to the publisher and no
retransmission is attempted**. The disconnect closes the session, the session
state is either discarded (clean session) or retained without the lost message
(persistent session). The publisher's `PUBLISH` already received a successful
path through the broker; the broker's obligation ends at `OutboundQueue::push`.

---

## Bug: Exception Storm on QoS 0 Drop Under Load (Fixed)

### Symptom

Under high publish rate (e.g. 4000 QoS 0 messages/second), when the subscriber
cannot keep up and the `OutboundQueue` fills to capacity (`max_queued_messages`,
default 100), CPU on an ARM Raspberry Pi stays at 100% during the overload
period — and remained at 100% for an extended period after the test ended.

### Root Cause

Before the fix, `deliver_function` in `BrokerModuleFactory` did this when
`OutboundQueue::push()` returned false:

```cpp
throw MessageRouterException(MessageRouterError::QueueFull,
                             "online outbound queue capacity exceeded");
```

This exception propagated through:

```
deliver_() throws
  → dispatch_item() propagates (no catch)
  → MessageRouter::route() — the for-loop over subscribers BREAKS
    (remaining subscribers in the same route call do not receive the message)
  → PublishFacade::handle_publish() catches → returns QuotaExceeded
  → runtime_step: for QoS 0 the return code is ignored (no PUBACK)
```

**Cost on ARM Cortex-A7/A53:** C++ exception handling uses DWARF unwind tables.
Each throw+catch costs hundreds of microseconds of unwinding on ARM.
At 3800 drops/second (4000/s published − 200/s forwarded), this consumed
one full CPU core continuously.

**Secondary bug:** The `route()` for-loop breaks on the first exception.
If multiple subscribers match the topic, all subscribers after the one with
the full queue do not receive the message in the same route call.

### Fix

In `broker_module_factory.cpp`, `deliver_function`:
```cpp
if (!pushed) {
    // ... trace ...
    // QoS 0: silent drop — no exception, no CPU overhead.
    // QoS 1/2: throw so the caller sends an error PUBACK/PUBREC.
    if (message.qos != QoS::AtMostOnce) {
        throw MessageRouterException(MessageRouterError::QueueFull, ...);
    }
    return;
}
```

For QoS 0: the dropped message is traced at Info level and silently discarded.
No exception. No stack unwinding. `wake_outbound_callback` is not called
(because the push failed), so no spurious DrainJob is submitted either.

### Why the CPU stayed high after the test ended

After the overload period, the Python MQTT client library (paho) has an
internal send queue. When the test "ends" (publisher stops calling
`publish()`), paho continues draining its internal queue over the TCP
connection. The broker kept receiving messages at high rate and kept
throwing exceptions per dropped message. With the exception removed, the
broker processes the tail of the paho queue at near-zero overhead per
dropped message.

### Thread implications

The exception path was triggered on the **Worker thread** executing
`process_decode_job` for the publisher's fd. The exception was caught in
`PublishFacade::handle_publish()` and did NOT propagate to `worker_loop()`.
Therefore no worker threads were killed. The `JobScheduler::mark_done()`
call was always reached. The broker continued functioning — only CPU was
wasted.

---

## Polling vs Event-Driven Summary

| Component | Mechanism | Polls? |
|-----------|-----------|--------|
| IoReactor | `kevent`/`epoll_wait` | **No.** Blocks until OS event. |
| Worker threads | `JobQueue::pop_blocking()` → condition variable | **No.** Sleep until job is pushed. |
| KeepaliveWatchdog | `cv.wait_for(100ms)` or `cv.wait_until(deadline)` | **Bounded poll.** Wakes at most every 100 ms, or at the earliest keep-alive / retransmit deadline. Only submits `DecodeJob`s for timed-out connections — does not touch MQTT state directly. |
| `OutboundQueue` | `try_pop()` called from `drain_outbound()` in worker | **No polling.** Only called when a `DrainJob` is already being executed (which was triggered by `wake_outbound`). |
| `JobScheduler` backlog | checked synchronously inside `submit()` and `mark_done()` | Not a poll — `mark_done()` is called exactly once per completed job. |
