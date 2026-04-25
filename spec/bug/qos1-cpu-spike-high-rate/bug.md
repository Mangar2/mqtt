# Bug: qos1-cpu-spike-high-rate

## user report

eröffne einen neuen bug. Meldung: bei hoher rate an gesendeten qos1 meldungen wird am broker plötzlich eine hoche cpu last erzeugt (sprung von 5% cpu auf > 200% cpu last) und die anzahl der verarbeiteten meldungen sinkt schlagartig . Die grenze ist etwa 11000 meldungen pro sekunde. Erstelle dazu einen spezialisierten testcase, der 20000 meldungen an qapla sendet (QoS1 meldungen) nach genau dem prinzip, den du vorher im performance -testfall gehab hast. laufzeit 10s senden, insgesamt 20s warten (also 10s nachlauf), dann beenden. success: die meldungen kommen alle innerhalb von 20s zurück. Testfall bitte direkt starten wenn er fertig ist.

## test case

- python3 spec/bug/qos1-cpu-spike-high-rate/repro_qos1_cpu_spike_high_rate.py --host qapla --port 1883

Expected pass condition:

- RESULT arrival_in_20s: PASS
- sent_unique == 20000 and received_unique_by_20s == 20000

Execution result (2026-04-25):

- RESULT preflight: FAIL target_unreachable=[Errno 61] Connection refused
- Exit code: 2

Related connectivity check:

- python3 test/run_performance_tests.py --host qapla --filter P02 --size small
- preflight result: target 192.168.0.150:1883 refused connection

Re-run after broker restart (2026-04-25):

- attempted=20000
- sent=20000
- send_errors=0
- acked=20000
- received_unique_by_20s=20000
- RESULT arrival_in_20s: PASS
- Exit code: 0

## scope

### allow
- Reproduction behavior of the dedicated testcase only.
- Observable symptom from user report: CPU spike and throughput drop around high QoS1 send rate.
- Runtime evidence collection commands and test outputs.

### deny
- Any code-path/source analysis not explicitly requested by user.
- Unrelated module exploration and speculative root-cause checks.

## confirmed facts

- Reported symptom: CPU jump from ~5% to >200% around ~11000 msg/s with throughput drop.
- Dedicated testcase required: 20000 QoS1 messages, 10s send, 20s total wait.
- After broker restart, the dedicated 20000/10s testcase against `qapla` passed (all returned within 20s).

## hypothesis

Whole-flow buffer scan (non-StreamBuffer) completed.

High-confidence O(n) buffer candidate:

- `src/transport/websocket_frame_codec.cpp`
	- `frames_.erase(frames_.begin())` in `consume_frame()`
	- `buf_.erase(buf_.begin(), buf_.begin() + total_size)` in `try_decode_()`
	- both are front erases on `std::vector`, which shift remaining elements and are O(n).

Additional O(n) list behavior (not front-erase buffer queue, but still linear per operation):

- `src/store/inflight_store.cpp`
	- per-client entries are stored in `std::vector<InflightEntry>` (`src/store/inflight_store.h`)
	- lookup uses linear `find_if`
	- remove uses `list.erase(entry_iter)`
	- operations are O(n) in number of inflight entries per client.

Structures checked and not matching old vector-front-removal pattern:

- `src/network/connection_slot.cpp` uses ring-buffer style indices (`write_head_index_`, `write_used_size_`) for pop/push; no front erase.
- `src/message_router/offline_queue.h` uses `std::deque<QueuedMessage>` with `pop_front`/`push_back`.
- `src/outbound_queue/outbound_queue.h` uses `std::queue<Message>` with FIFO pop.
- `src/client_session/client_session.h` deferred messages use `std::deque<Message>`.
