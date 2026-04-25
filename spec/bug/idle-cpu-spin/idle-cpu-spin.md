# Bug: idle-cpu-spin

## user report

1. das script erstellt überlast mit QoS0 meldungen.
2. das script wird beendet, damit ist weder ein publisher noch ein empfäger noch da. es gibt keine weiteren meldungen die laufen.
Dennoch braucht der broker dauerhaft und auch beliebig lange zeit 100% cpu.
Das passiert ausschliesslich, wenn überlast erzeugt wurde, d.h. die meldungen konnten nicht mehr alle verarbeitet werden.
Das problem ist schon lange da.

## scope

### allow
- executor worker thread(s) — idle behavior after overload
- scheduler state after all connections are closed
- any queue/backlog state that survives client disconnect
- QoS0 receive path (what happens when broker cannot keep up)
- job lifecycle: submit → drop → mark_done

### deny
- QoS1 / QoS2 retransmit, inflight, ack
- retained messages
- will messages
- persistence / disk I/O
- PUBLISH processing internals (decode, routing)
- subscription matching

## confirmed facts

- trigger: send ~4000 QoS0 messages/sec (e.g. `python3 test/run_performance_tests.py --host raspberrypi --size middle --filter P01`)
- after test script ends: zero publishers, zero subscribers, zero active connections
- broker stays at ~100% CPU indefinitely after that
- only happens after overload (messages could not all be processed)
- problem exists for a long time (not a recent regression)
- platform: Raspberry Pi armv6

## hypothesis

**Suspect:** `src/network/io_reactor_epoll.cpp` — level-triggered epoll spin

**Mechanism (step 3 analysis):**

1. epoll is registered as `EPOLLIN | EPOLLRDHUP` — NO `EPOLLET` flag → **level-triggered**
2. During overload the client sends faster than the broker can process. The Decode job hits `k_decode_packet_budget` (32 packets) and self-reschedules. This is correct behavior, but causes the stream_buffer to accumulate a large backlog of unprocessed MQTT packets.
3. Client disconnects (test ends). The TCP fd now has a permanent HUP/RDHUP/ERR condition.
4. Level-triggered epoll: `epoll_wait` returns that fd IMMEDIATELY on every call, forever, as long as the fd is registered.
5. Each epoll_wait return → read_callback → `scheduler.submit(Decode)` → **dropped** (`backlog_contains_type` dedup: backlog already has a Decode from self-reschedule).
6. The reactor thread spins in a tight loop: `epoll_wait` → submit → drop → `epoll_wait` → submit → drop → ...
7. Meanwhile ONE worker processes Decode jobs slowly, draining the stream_buffer 32 packets at a time.
8. Drain completes when: stream_buffer empty + `peer_closed=true` detected → `close_after_flush=true` → `submit_close` → `reactor.unregister(fd)` → epoll stops firing → spin ends.

**Why only after overload:** Under normal load the stream_buffer is small → drain fast → spin brief. Under overload (4000 msg/sec × test duration) the buffer can hold hundreds of thousands of packets → drain takes many seconds → reactor spins for seconds/minutes.

**Why "beliebig lange":** Spin duration is proportional to overload duration. Longer test = more buffered data = longer drain = longer spin.

**Confidence:** HIGH — level-triggered epoll confirmed by code, no EPOLLET flag, behavior matches all symptoms exactly.

**Files implicated:**
- `src/network/io_reactor_epoll.cpp` line 183/197: registration without EPOLLET
- `src/executor/job_scheduler.cpp`: `backlog_contains_type` dedup causes the drop storm
- `src/connection/client_handler.cpp`: `peer_closed` detection only stops the spin after drain completes

**Proposed fix:** When `peer_closed=true` is detected in the read loop of `process_decode_job`, immediately call `reactor.disarm_read(fd)` (or equivalent: remove EPOLLIN+EPOLLRDHUP from the fd's epoll registration). The stream_buffer drain continues via self-reschedule (no reactor needed), and the reactor stops spinning for that fd. The `process_close_job` call to `reactor.unregister` then cleans up completely.

## added traces (uncommitted)

All traces added in previous sessions to investigate this and related paths.
None have a `// BUG-TRACE-TEMP` marker yet — to be added once scope is confirmed.

### src/connection/client_handler.cpp

| trace info | location | fields | purpose |
|---|---|---|---|
| `decode_job_enter` | top of `process_decode_job` | fd | every decode job invocation |
| `decode_guard_busy` | if `!decode_guard.active()` | fd | job dropped because fd already decoding |
| `decode_read_error` | after read loop error branch | fd | read syscall error path |
| `decode_phase_read_done` | after read loop | fd, total_read, peer_closed | bytes read this job |
| `decode_packet_begin` | before each `decode_one_packet` | fd, n | packet loop entry |
| `decode_packet_end` | after each `decode_one_packet` | fd, n, outcome | packet loop exit |
| `decode_phase_packets_done` | after packet loop | fd, packets, close_after_flush | how many packets in this job |
| `decode_phase_drain_done` | after `drain_outbound_to_write_buffer` | fd, write_buf, close_after_flush | write buffer size after drain |
| `close_job_start` | top of `process_close_job` | fd | close job entry |
| `close_job_unregister` | before `reactor.unregister` in close | fd | just before fd is removed from epoll |

Also: `reactor.unregister(fd)` added when `entry == nullptr` in `process_decode_job`.

### src/executor/job_scheduler.cpp

| trace info | location | fields | purpose |
|---|---|---|---|
| `scheduler_submit_active` | in `submit()`, before backlog check | connection_fd, job_type, active_type, backlog_size | every submit that reaches scheduling |
| `scheduler_job_dropped` | in `submit()`, backlog dedup drop | connection_fd, job_type, active_type, backlog_size | duplicate job suppressed |
| `mark_done_unknown_fd` | in `mark_done()`, fd not found | connection_fd | fd already removed from states |
| `mark_done` | in `mark_done()`, fd found | connection_fd, backlog_size | job completion, shows backlog depth |

### src/executor/worker_pool.cpp / worker_pool.h

| trace info | location | fields | purpose |
|---|---|---|---|
| `worker_job_pop` | before `job_handler_(*job)` | connection_fd, job_type | worker dequeued a job |

Also: `tracer_` member added to `WorkerPool`; `job_handler_` call wrapped in try/catch.

### src/network/io_reactor.h / io_reactor_epoll.cpp / connection_manager.cpp

| trace info | location | fields | purpose |
|---|---|---|---|
| `reactor_event` | epoll run_loop, throttled 1/sec per fd | fd, flags (IN/OUT/RDHUP/HUP/ERR), events_per_sec | epoll events firing rate |
| `reactor_epollout` | when EPOLLOUT fires | fd | write-ready event |

Also: `IoReactor` constructor gains `StructuredTracer*` param; `connection_manager.cpp` passes `&broker_.structured_tracer()`.

## test case to use

Official testcase for this bug:

- `python3 spec/bug/idle-cpu-spin/run_idle_cpu_spin_case.py`

How to use it:

1. default run (no traces):
   - `python3 spec/bug/idle-cpu-spin/run_idle_cpu_spin_case.py`
2. run with startup traces (only configurable inputs):
   - `python3 spec/bug/idle-cpu-spin/run_idle_cpu_spin_case.py --trace-level info --trace-module executor --trace-module connection`
3. read result summary from script output (`RESULT summary ...`)
4. inspect CPU snapshot on Raspberry Pi after load run

**IMPORTANT — always run in foreground (sync mode):**
The testcase takes ~60–90 seconds. It MUST be run synchronously so the agent blocks until it completes and receives the full output including `RESULT cpu_tail_start/end`. Never run it in async/background mode. Do not poll, do not check intermediate output. Wait for the single complete result before taking any action.

Runner behavior (hard coded):

1. build and deploy broker via `test/deploypi.py`
2. run broker on `pi@raspberrypi` in `/home/pi/mqtt/idle-cpu-spin`
3. execute a short P01-like QoS0 load with fixed `4000 msg/s`
4. after load, execute `top -H -b -n 1 -p $(pgrep -n -x mqtt-broker)` on Raspberry Pi
5. store artifacts on Raspberry Pi (not on macOS)

Remote artifact files:

- `/home/pi/mqtt/idle-cpu-spin/broker.log`
- `/home/pi/mqtt/idle-cpu-spin/idle-cpu-spin-result.json`
- `/home/pi/mqtt/idle-cpu-spin/idle-cpu-spin-top-after-load.txt`

## analysis log

### session 2026-04-25

Step 1: bug file created.
Step 2: scope locked — awaiting user confirmation.
Step 3: hypothesis written — level-triggered epoll spin after overload. Confidence high.
Step 4: trace run designed. Command to execute:

```
python3 spec/bug/idle-cpu-spin/run_idle_cpu_spin_case.py \
  --trace-level info \
  --trace-module connection \
  --trace-module executor
```

Evidence to confirm hypothesis from broker.log:
- `reactor_event` after test ends: flags contain RDHUP/HUP/ERR, `events_per_sec` >> 100 — proves epoll fires continuously for disconnected fd
- `scheduler_job_dropped` high call_rate for same fd (Decode type) at same time — proves submit→drop spin
- Both `reactor_event` and `scheduler_job_dropped` stop after `close_job_unregister` for that fd — proves spin ends exactly when fd is unregistered from epoll

Waiting for user to run the command and respond with "weitermachen".

### testcase execution 2026-04-25

Command:

- `python3 spec/bug/idle-cpu-spin/run_idle_cpu_spin_case.py`

Observed summary:

- sent=79993
- recv=2045
- requested_rate=4000/s
- achieved_send_rate=1673.2/s
- achieved_receive_rate=42.8/s
- elapsed=47.81s

Observed CPU snapshot after load (`top -H` on Raspberry Pi):

- system CPU: ~90.9% user, 9.1% system, 0.0% idle
- broker threads with high CPU: ~50% and ~40%

Artifacts from this run:

- `/home/pi/mqtt/idle-cpu-spin/idle-cpu-spin-result.json`
- `/home/pi/mqtt/idle-cpu-spin/idle-cpu-spin-top-after-load.txt`
- `/home/pi/mqtt/idle-cpu-spin/broker.log`

### trace analysis 2026-04-25 (Step 4)

**Testcase run (latest):**

- sent=79979, recv=772, elapsed=60.79s
- CPU snapshot: Thread 9827: 41.7%, Thread 9829: 33.3%, 0.0% idle → bug confirmed

**broker.log fetched to `/tmp/idle-cpu-spin-broker.log`** (31290 entries)

**Event counts in broker.log:**

| info | count |
|---|---|
| scheduler_submit_active | 10329 |
| scheduler_job_dropped | 10161 |
| decode_packet_begin / end | 2001 each |
| reactor_event | 99 |
| decode_job_enter | 76 |
| decode_phase_packets_done | 74 |
| close_job_unregister | 2 |

**Connection lifecycle:**

| time | event | fd |
|---|---|---|
| 13:01:28.855Z | connect_handled | — |
| 13:01:28.916Z | close_job_unregister | fd=8 (initial, closed immediately) |
| 13:01:29.080Z | connect_handled (publisher) | — |
| 13:01:29.082Z | connection_registered | — |
| 13:01:29.114Z | decode_job_idle | fd=8 (new publisher on reused fd=8) |
| 13:02:29.836Z | close_job_start | fd=9 (subscriber MQTT DISCONNECT) |
| 13:02:29.891Z | close_job_unregister | fd=9 |
| — | close_job_unregister fd=8 | **NEVER** — publisher fd=8 never unregistered |

**fd=8 = publisher** (sent 79,979 PUBLISH QoS0, dropped TCP without MQTT DISCONNECT → kernel buffer holds unread data).

**Reactor spin after publisher closes (after 13:02:29):**

- `reactor_event` fd=8 flags=IN,RDHUP at 41–129 events/sec continuously
- `scheduler_job_dropped` fd=8 Decode at 15–127 drops/sec continuously
- **spin duration: 35.3 seconds** (13:02:29 → 13:03:05)
- `close_job_unregister` fd=8 **never seen** in log window → spin was still ongoing at log end

**Decode drain after publisher closes:**

- `decode_phase_read_done` fd=8: `total_read=65536 peer_closed=false` (×13) — kernel buffer still has data
- `decode_phase_packets_done` fd=8: 14 entries × 32 packets = 448 packets processed in 35s
- Each Decode job takes ~2–3 seconds; reactor fires 100+/sec → all submits dropped

**Why peer_closed=false during drain:**  
The kernel TCP receive buffer holds the publisher's data AHEAD of the EOF marker. `recv()` returns 65536 bytes successfully. RDHUP is visible in epoll but `recv()` returns 0 (peer_closed) only AFTER all buffered data is drained.

**Causal chain — CONFIRMED:**

1. Overload → publisher sends ~80,000 messages → kernel receive buffer fills (~832 KB = ~1660 packets)
2. Publisher closes TCP → kernel marks EOF after buffered data → epoll shows IN|RDHUP for fd=8
3. Level-triggered epoll: fires 100+/sec forever while data is in kernel buffer OR after EOF
4. Each fire → `read_callback(fd=8)` → `submit(Decode)` → **DROPPED** (Decode already in backlog from self-reschedule)
5. Reactor thread spins: epoll_wait → submit → drop at 100+/sec
6. Worker thread runs Decode every 2–3s: reads 65536 bytes, processes 32 packets, self-reschedules
7. Spin ends only when: all kernel data drained → `recv()` returns 0 → `peer_closed=true` → stream_buffer drains → `close_after_flush=true` → `submit_close` → `reactor.unregister(fd=8)`
8. `close_job_unregister fd=8` never appears in log → drain was still in progress at log capture time

**Hypothesis status: CONFIRMED (Step 4 complete)**

**Updated proposed fix:**

The original proposed fix (`disarm_read` when `peer_closed=true`) is correct in principle but triggers too late — only AFTER the kernel buffer is fully drained, not during the drain. A better fix:

- In `IoReactorEpoll::process_event()`: when epoll returns RDHUP|HUP|ERR flags for an fd, call `disarm_read(fd)` BEFORE calling `read_callback(fd)`.
- This stops the reactor spin on the FIRST RDHUP event.
- The Decode job continues draining via self-reschedule (`packet_budget_exhausted` or `read_budget_exhausted` path) — no reactor needed during drain.
- Final close: `recv()` returns 0 → `peer_closed=true` → stream_buffer empties → `submit_close` → `process_close_job` calls `reactor.unregister(fd)` (safe even after disarm).

This is a 2–3 line change in `io_reactor_epoll.cpp`.

## resolution

**Status: FIXED**

**Fix location:** `src/network/io_reactor_epoll.cpp`, `run_loop()`, event dispatch block.

**Fix (7 lines added):**

```cpp
if (((event_flags & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)) != 0U) &&
    read_callback) {
  if ((event_flags & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) != 0U) {
    // Peer closed or error: disarm read to prevent level-triggered spin.
    // The Decode job continues draining the kernel receive buffer via
    // self-reschedule; the reactor does not need to fire again for this fd.
    epoll_event ev{};
    ev.data.fd = socket_fd;
    ev.events = 0;
    (void)::epoll_ctl(backend_fd_, EPOLL_CTL_MOD, socket_fd, &ev);
  }
  read_callback(socket_fd);
}
```

**Why this works:**

1. On first RDHUP/HUP/ERR: `EPOLL_CTL_MOD` with `events=0` removes all epoll events for this fd → reactor never fires for it again.
2. `read_callback(socket_fd)` is still called once → ensures Decode job is in the backlog (or already running).
3. Decode job drains the kernel receive buffer via self-reschedule (`packet_budget_exhausted` or `read_budget_exhausted`) without any reactor involvement.
4. Once kernel buffer empty: `recv()` returns 0 → `peer_closed=true` → stream_buffer drains → `submit_close`.
5. `process_close_job` calls `reactor.unregister(fd)` → `EPOLL_CTL_DEL` (safe after previous MOD).

**Verification result (testcase run after fix):**

- sent=79992, recv=727, elapsed=59.99s (same load as before)
- CPU snapshot: **no broker threads visible in `top -H`** — completely idle after test ends
- load average: 1.54, 0.74, 0.52 (falling, not sustained)
- Compare: pre-fix: threads at 41.7% + 33.3%, 0% idle, spin lasting 35+ seconds

**Fix is minimal and correct.** No new methods, no state changes, no impact on write path or normal disconnect path.
