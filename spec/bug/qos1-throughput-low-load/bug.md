# Bug: qos1-throughput-low-load

## active bug focus

- This bug is about broker send throughput in QoS1 publish fan-out.
- Exact symptom to fix: after an initial good phase, broker send path becomes slower than broker receive path, so in P02 `sent` and `recv` diverge over time.
- Primary verification signal: in the P02 timeline output, `sent` per line should stay approximately equal to `recv` per line until scenario end.
- Queue-full effects are a consequence to analyze only in relation to this throughput divergence, not a separate bug target; the analysis must explicitly explain why the outbound queue fills in the first place.

Hard prohibition for all further work on this bug:

- Absolute focus: explain only why outbound writing/draining is too slow although machine load is low.
- Forbidden: any analysis, hypothesis, or code search outside this exact topic.
- Forbidden: discussing queue limits, config defaults, trace-volume tuning, or any other side topic as primary cause.
- Forbidden: expanding scope to unrelated protocol areas (QoS0/QoS2/retained/will/persistence/auth).
- Every step must answer only this question: why does the write/drain path not keep up with inbound publish rate?

## test prerequisite

- Before any fix verification with `python3 test/run_performance_tests.py --host qapla --filter P02 --size middle`:
	1. Rebuild broker binary with current code.
	2. Deploy/restart broker on target host.
- Running the performance script against a stale broker process does not validate the current fix.

## one-pass analysis capture (full trace set)

Goal:

- Capture enough runtime evidence in one expensive run to isolate why broker send throughput falls behind receive throughput in P02.

Scope for this capture:

- QoS1 publish receive path, broker route/deliver path, outbound drain/write scheduling path.

Required broker start (with full trace coverage in one run):

- Run this directly on qapla shell (not via deploy helper).
- Binary path is fixed: `./build/release/mqtt-broker`.
- Config path is fixed: `test/broker.ws.ini`.
- Trace must be written to a dedicated file in the broker working directory: `./broker-p02-analysis.trace`.
- Mandatory command style for this bug: provide only the pure broker process invocation.
- Forbidden in the broker command: `cd`, `pkill`, `mkdir`, `nohup`, ssh wrappers, or chained shell control operators.

```sh
./build/release/mqtt-broker test/broker.ws.ini \
	--trace-level=trace \
	--trace-module=broker \
	--trace-module=connection \
	--trace-module=executor \
	--trace-module=message_router \
	--trace-module=session_manager \
	--trace-module=subscription_manager \
	--trace-module=transport \
	> ./broker-p02-analysis.trace 2>&1
```

Reproduction command (unchanged verification path):

```sh
python3 test/run_performance_tests.py --host qapla --filter P02 --size middle
```

Copy trace artifact from qapla to local bug directory after run:

```sh
mkdir -p spec/bug/qos1-throughput-low-load/artifacts
scp -o BatchMode=yes \
	mangar@qapla:/home/mangar/dev/mqtt/broker-p02-analysis.trace \
	spec/bug/qos1-throughput-low-load/artifacts/
```

Optional copy of the exact runner summary used for correlation:

```sh
cp test/performance_test_results.json \
	spec/bug/qos1-throughput-low-load/artifacts/performance_test_results.p02.json
```

Note:

- This section is the default analysis procedure for this bug. Reuse it for future analysis iterations to avoid incomplete trace capture.
- Command quality rule for this bug: never provide wrapper or orchestration commands when asked for broker invocation; return only the direct broker process call with explicit trace output file.

## user report

aber davon abgesehen. Wir haben zwei fehler. Einen kennen wir: der broker schafft trotz niedriger last keinen vernünftigen durchsatz bei QoS1 meldungen 4000 Meldungen pro sekunde sollte er schaffen schon bei 231 kommt er nicht mehr mit. 2. Das ist nur ein fehler verdacht - nicht alle QoS1 meldungen kommen an, es werden meldungen verschluckt. Beide fehler sind zu behandeln und in einem sicheren testfall nachzustellen der den fehler konkret beweist und direkt und einduetig als ergebnis ausgibt.

## test case

Dedizierter Arrival-basierter Beweistest (gleiche Technik wie vorheriger dedizierter QoS1-Test):

- python3 spec/bug/qos1-throughput-low-load/repro_qos1_arrival_4000s_10plus10plus5.py --host qapla --port 1883

Ergebnisformat (eindeutig):

- RESULT arrival_in_20s: PASS|FAIL
- INFO post_5s_arrivals: count=<n> unique=<n>
- EXIT CODE 0 bei PASS, sonst 1

Szenario-Definition:

- Anzahl Publisher/Subscriber ist parametrisierbar (`--publishers`, `--subscribers`)
- Host ist parametrisierbar (`--host`)
- aktueller Default: 2 Clients gesamt (1 Publisher, 1 Subscriber)
- pro Publisher standardmaessig 400 QoS1 Publish/s fuer 10s (`--send-rate-per-publisher` parametrisierbar)
- 1:1 Topic-Paarung (Publisher i -> Subscriber i, daher Publisherzahl == Subscriberzahl)
- Auswertung bei insgesamt 20s Laufzeit: alle gesendeten QoS1 muessen angekommen sein
- danach zusaetzlich 5s Beobachtung, ob noch Meldungen nachkommen

Letzter Lauf (2026-04-25):

- attempted=69309
- accepted=69280
- queue_full_rejects=29
- received=3835
- active_elapsed=31.480s
- RESULT throughput: FAIL actual=121.82/s required_min=4000.00/s
- exit code: 1

Dedizierter Arrival-Lauf (2026-04-25, 4000/s):

- target_sent=40000
- attempted=40000
- sent=40000
- send_errors=0
- acked=24294
- received_unique_by_20s=19274
- missing_by_20s=20726
- RESULT arrival_in_20s: FAIL
- INFO post_5s_arrivals: count=5020 unique=5020
- exit code: 1

Dedizierter Arrival-Lauf (2026-04-25, 2000/s):

- target_sent=20000
- attempted=20000
- sent=20000
- send_errors=0
- acked=14566
- received_unique_by_20s=9806
- missing_by_20s=10194
- RESULT arrival_in_20s: FAIL
- INFO post_5s_arrivals: count=4771 unique=4771
- exit code: 1

Dedizierter Arrival-Lauf (2026-04-25, 10 Clients: 5 Pub + 5 Sub, 400/s je Publisher):

- target_sent=20000
- attempted=20000
- sent=20000
- send_errors=0
- acked=20000
- received_unique_by_20s=20000
- missing_by_20s=0
- RESULT arrival_in_20s: PASS
- INFO post_5s_arrivals: count=0 unique=0
- exit code: 0

Dedizierter Arrival-Lauf (2026-04-25, qapla, Default 2 Clients):

- target_sent=4000
- attempted=4000
- sent=4000
- send_errors=0
- acked=4000
- received_unique_by_20s=4000
- missing_by_20s=0
- RESULT arrival_in_20s: PASS
- INFO post_5s_arrivals: count=0 unique=0
- exit code: 0

Zusaetzlicher bereits beobachteter Referenzlauf:

- python3 test/run_performance_tests.py --host raspberrypi --filter P02 --size small
- status: DEVIATION
- sent=25521, received=16242, throughput=134.22 msg/s, ramp_stopped_at_rate=231.11/s

## scope

### allow
- P02 QoS1 throughput behavior
- publisher ACK completion rate
- subscriber delivery rate
- relation to reported low broker CPU load

### deny
- QoS0 and QoS2 scenarios
- retained, will, persistence behavior
- protocol features outside QoS1 publish/ack/deliver path
- any analysis not directly tied to outbound write/drain under low CPU load

## confirmed facts

- User reports low broker CPU load (<10%) while throughput collapses for QoS1.
- Existing P02 run reports strong deviation and low receive throughput.
- Requested expected capability is 4000 QoS1 msg/s.

## open ambiguities

- CPU measurement basis is not yet captured in testcase artifact (total/system/thread-level, sampling interval).
- Exact acceptance threshold for throughput-proof run is not yet fixed in code (using 4000 msg/s as requested target).


## protocol update 2026-04-26

New validation runs with scaled P02 clients:

- Command: `python3 test/run_performance_tests.py --host qapla --filter P02 --size middle`
- Scenario configuration log: `subscribers=10 dynamic_pub_clients=10..N max_rate_per_client=2000/s`
- Result: `FAIL (DEVIATION)`
- Summary values: `sent=1004794`, `received=587698`, `throughput=4856.62 msg/s`, `pub_clients=60`

- Command: `python3 test/run_performance_tests.py --host localhost --filter P02 --size middle`
- Scenario configuration log: `subscribers=10 dynamic_pub_clients=10..N max_rate_per_client=2000/s`
- Result: `FAIL (DEVIATION)`
- Summary values: `sent=1004536`, `received=556657`, `throughput=4599.78 msg/s`, `pub_clients=60`

Recorded observations from this comparison:

- Increasing sender and receiver client count by factor 10 did not remove the P02 deviation.
- The same failure pattern appears on remote host and on localhost.
- With localhost result in the same range as remote, network bandwidth and network latency are not the primary bottleneck for this bug.

Working assumptions agreed in this analysis iteration:

- OS socket transport on one machine has high reserve for this payload size and is not treated as the main limiter.
- Python client library is assumed to have large remaining headroom (order-of-magnitude reserve) and is not treated as the main limiter in this iteration.

Current primary hypothesis after this update:

- The dominant bottleneck is in broker-internal outbound processing (fan-out write/drain path), especially scheduling/fairness/queue-drain behavior across many active subscriber connections.

## protocol update 2026-04-26 (parallel dual-broker run, halved load)

Testcase adjustments used for this run:

- P02 dual-broker alternation was reverted (single broker per run via `--port`).
- P02 load was halved for all size profiles.
	- `small`: `10->2000/s` to `5->1000/s`
	- `middle`: `100->20000/s` to `50->10000/s`
	- `large`: `1000->100000/s` to `500->50000/s`

Parallel run A (broker on port 1883):

- Command: `python3 test/run_performance_tests.py --host localhost --port 1883 --filter P02 --size middle`
- Result: `FAIL (DEVIATION)`
- Summary values: `sent=702408`, `received=599518`, `throughput=4954.52 msg/s`, `pub_clients=5`

Parallel run B (broker on port 11883):

- Command: `python3 test/run_performance_tests.py --host localhost --port 11883 --filter P02 --size middle`
- Result: `FAIL (DEVIATION)`
- Summary values: `sent=702477`, `received=606856`, `throughput=5015.29 msg/s`, `pub_clients=5`

Recorded observations from this parallel comparison:

- Both independent broker instances show nearly identical deviation behavior under the same halved P02 profile.
- Divergence remains (`sent` noticeably above `received`) on both ports.
- This behavior is therefore not tied to one specific broker instance or one port.

## protocol update 2026-04-26 (client-side limitation hypothesis)

Additional interpretation agreed after the latest counter-tests:

- A single run that internally distributes pairs across two brokers does not materially improve behavior versus a single run against one broker.
- Two independent client runs in parallel (one per broker) increase aggregate throughput more clearly.

Working conclusion from these observations:

- The throughput limitation is most likely dominated by the Python client/test-runner side (single-run pacing/drain/scheduling behavior), not by a single broker instance.
