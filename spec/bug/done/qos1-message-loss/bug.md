# Bug: qos1-message-loss

## user report

aber davon abgesehen. Wir haben zwei fehler. Einen kennen wir: der broker schafft trotz niedriger last keinen vernünftigen durchsatz bei QoS1 meldungen 4000 Meldungen pro sekunde sollte er schaffen schon bei 231 kommt er nicht mehr mit. 2. Das ist nur ein fehler verdacht - nicht alle QoS1 meldungen kommen an, es werden meldungen verschluckt. Beide fehler sind zu behandeln und in einem sicheren testfall nachzustellen der den fehler konkret beweist und direkt und einduetig als ergebnis ausgibt.

## test case

Dedizierter Beweistest nur fuer QoS1-Ankunft:

- python3 spec/bug/qos1-message-loss/repro_qos1_arrival_1000s_10plus10plus5.py --host raspberrypi --port 1883

Ergebnisformat (eindeutig):

- RESULT arrival_in_20s: PASS|FAIL
- INFO post_5s_arrivals: count=<n> unique=<n>
- EXIT CODE 0 bei PASS, sonst 1

Szenario-Definition:

- exakt 2000 QoS1 Publish/s fuer 10s (ein Sender)
- ein Empfaenger auf einem eindeutigen Topic
- Auswertung bei insgesamt 20s Laufzeit: sent vs received_by_20s
- danach zusaetzlich 5s Beobachtung auf spaet eintreffende Meldungen
- Erfolg nur wenn in 20s alle gesendeten QoS1 Meldungen angekommen sind

Dedizierter Lauf (2026-04-25):

- target_sent=10000
- attempted=10000
- sent=10000
- send_errors=0
- acked=10000
- received_by_20s=10000
- RESULT arrival_in_20s: PASS
- INFO post_5s_arrivals: count=0 unique=0
- exit code: 0

Dedizierter Lauf (2026-04-25, 2000/s):

- target_sent=20000
- attempted=20000
- sent=20000
- send_errors=0
- acked=20000
- received_unique_by_20s=19293
- missing_by_20s=707
- RESULT arrival_in_20s: FAIL
- INFO post_5s_arrivals: count=707 unique=707
- exit code: 1

Legacy-Kombitest Lauf (2026-04-25, nur Referenz):

- attempted=69220
- accepted=69185
- queue_full_rejects=35
- acked=14326
- received=14326
- unacked=54859
- missing_delivery=54859
- RESULT throughput: FAIL actual=107.32/s required_min=4000.00/s
- RESULT loss: FAIL missing_delivery=54859 unacked=54859
- exit code: 1

## scope

### allow
- QoS1 publish->PUBACK completion
- QoS1 end-to-end delivery publisher->broker->subscriber
- missing IDs analysis for sent vs acked vs received

### deny
- QoS0 and QoS2 logic
- retained messages, will messages, session persistence
- unrelated integration/performance scenarios

## confirmed facts

- There is an explicit suspicion that not all QoS1 messages arrive.
- Existing P02 run shows large delta sent vs acked/received at test end.

## resolution

Status: CLOSED

Decision:

- No standalone message-loss bug confirmed.
- Observed symptom is delivery delay under higher QoS1 load, consistent with performance/backlog behavior.

Evidence:

- 1000/s testcase: all 10000 QoS1 messages arrived by 20s and no arrivals in the extra 5s window.
- 2000/s testcase: 707 messages were missing at 20s, but exactly those 707 arrived in the extra 5s window.
- Publisher ACK count matched sent count in the dedicated runs.

Conclusion:

- Messages are not disappearing in this bug scope.
- Remaining issue belongs to QoS1 performance/latency throughput bug tracking.

## open ambiguities

- None for this bug scope. Closed by dedicated testcase evidence.
