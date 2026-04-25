# Bug: qos1-throughput-low-load

## user report

aber davon abgesehen. Wir haben zwei fehler. Einen kennen wir: der broker schafft trotz niedriger last keinen vernünftigen durchsatz bei QoS1 meldungen 4000 Meldungen pro sekunde sollte er schaffen schon bei 231 kommt er nicht mehr mit. 2. Das ist nur ein fehler verdacht - nicht alle QoS1 meldungen kommen an, es werden meldungen verschluckt. Beide fehler sind zu behandeln und in einem sicheren testfall nachzustellen der den fehler konkret beweist und direkt und einduetig als ergebnis ausgibt.

## test case

Dedizierter Arrival-basierter Beweistest (gleiche Technik wie vorheriger dedizierter QoS1-Test):

- python3 spec/bug/qos1-throughput-low-load/repro_qos1_arrival_4000s_10plus10plus5.py --host raspberrypi --port 1883

Ergebnisformat (eindeutig):

- RESULT arrival_in_20s: PASS|FAIL
- INFO post_5s_arrivals: count=<n> unique=<n>
- EXIT CODE 0 bei PASS, sonst 1

Szenario-Definition:

- exakt 4000 QoS1 Publish/s fuer 10s (ein Sender)
- ein Empfaenger auf einem eindeutigen Topic
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

## confirmed facts

- User reports low broker CPU load (<10%) while throughput collapses for QoS1.
- Existing P02 run reports strong deviation and low receive throughput.
- Requested expected capability is 4000 QoS1 msg/s.

## open ambiguities

- CPU measurement basis is not yet captured in testcase artifact (total/system/thread-level, sampling interval).
- Exact acceptance threshold for throughput-proof run is not yet fixed in code (using 4000 msg/s as requested target).
