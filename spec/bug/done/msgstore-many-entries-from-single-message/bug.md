## user report
leider funktioniert der message store service client nicht korrekt. Hier ein ausschnitt von dem, was ich aus dem messagestore client bekomme. Die meldungen kommen alle 10 sekunden (maximale häufigkeit), daher ist da offensichtlich ein fehler und es werden aus einer meldung viele einträge produziert. Versuche zunächst mit Hilfe eines Testfalls (unit testfall) oder wenn das niht geht mit einem integration testfall das problem zu reproduzieren: {
    "payload": [
        {
            "topic": "first/study/main/light/light on time",
            "value": 1200,
            "time": "2026-05-08T23:18:38.492Z",
            "reason": [
                {
                    "message": "received from arduino",
                    "timestamp": "2026-05-08T23:18:38.492Z"
                },
                {
                    "message": "received by broker",
                    "timestamp": "2026-05-08T23:18:38.496Z"
                }
            ],
            "history": [
                {
                    "time": "2026-05-08T23:18:38.492Z",
                    "value": 1200,
                    "reason": []
                },
                {
                    "time": "2026-05-08T23:18:38.492Z",
                    "value": 1200,
                    "reason": []
                },
                {
                    "time": "2026-05-08T23:18:38.492Z",
                    "value": 1200,
                    "reason": []
                },
                {
                    "time": "2026-05-08T23:18:38.492Z",
                    "value": 1200,
                    "reason": [
                        {
                            "message": "received from arduino",
                            "timestamp": "2026-05-08T23:18:38.492Z"
                        },
                        {
                            "message": "received by broker",
                            "timestamp": "2026-05-08T23:18:38.496Z"
                        }
                    ]
                },
                {
                    "time": "2026-05-08T22:47:57.528Z",
                    "value": 0,
                    "reason": []
                },
                {
                    "time": "2026-05-08T22:47:57.528Z",
                    "value": 0,
                    "reason": []
                },
                {
                    "time": "2026-05-08T22:47:57.528Z",
                    "value": 0,
                    "reason": []
                },

## test case
- Existing test in standard suite: history_multi_reason_same_device_timestamp_adds_exactly_one_logical_entry_per_update
- Run command: ./build/debug/yahabroker-tests "history_multi_reason_same_device_timestamp_adds_exactly_one_logical_entry_per_update" --reporter compact --colour-mode none
- Expected at Step 2: FAIL (to reproduce the original bug)
- Actual run output:
    - Filters: "history_multi_reason_same_device_timestamp_adds_exactly_one_logical_entry_per_update"
    - RNG seed: 2973264369
    - All tests passed (19 assertions in 1 test case)
- Added integration replay test in standard suite:
    - file: `test/integration_tests/robustness/msgstore_light_on_time_replay.py`
    - test name: `robustness/msgstore_replay_light_on_time_detects_stale_timestamp_projection`
    - run command: `python3 test/run_integration_tests.py --filter robustness/msgstore_replay_light_on_time_detects_stale_timestamp_projection --timeout 12`
    - input source: `spec/bug/msgstore-many-entries-from-single-message/yahamsgstoreclient_light_on_time_latest.txt`
    - current result after fix: PASS (`input_messages=9, unique_projected_times=9`)
    - previous failing detail (before fix): replay showed stale timestamp projection with sample `2026-05-09T05:16:43.000Z` repeated in current value and newest history entries
- Added unit regression test in standard suite:
    - file: `src/yaha/message_store/test/message_tree_test.cpp`
    - test name: `history_stale_first_reason_timestamp_does_not_collapse_new_updates`
    - run command: `./build/debug/yahabroker-tests "history_stale_first_reason_timestamp_does_not_collapse_new_updates" --reporter compact --colour-mode none`
    - current result after fix: PASS (all assertions pass)

## unit test gap analysis
- Why previous unit tests did not find this bug:
    - existing tests were mostly count-integrity focused (`requireTotalEntryCount`) and did not assert time progression semantics under stale-first-reason input
    - test `history_multi_reason_same_device_timestamp_adds_exactly_one_logical_entry_per_update` explicitly accepts collapsed timestamps by requiring all history timestamps to be equal
    - previous reasoned-message test helpers mostly used one reason or reason orders where freshest broker timestamp ended up first, so stale-first-reason ordering from field data was not covered
- Gap closure:
    - new failing unit regression test now reproduces stale timestamp collapse when first reason entry has old browser timestamp and later reason entries are newer broker timestamps

## confirmed facts
- Input side is the yahabrokerconnector client.
- The affected message arrives there exactly once.
- Output side is the GET call on the msgstore client.
- That single input message appears multiple times at output (observed up to 5 entries).
- Expected rule is strict 1:1 mapping: input = output.

## persisted storage evidence
- Source files copied from productive msgstore into `tmp/data`:
    - `tmp/data/message_store.bak_1778301929299.mtree`
    - `tmp/data/message_store.bak_1778301934791.mtree`
    - `tmp/data/message_store.bak_1778301940269.mtree`
- Topic checked: `first/study/main/light/light on time`
- Same result in all three files:
    - current value is `N 240`
    - `HISTORY_COUNT=268`
    - `HISTORY_ENTRIES_WITH_CURRENT_TIME=6`
- Concrete excerpt from `message_store.bak_1778301940269.mtree`:
    - H001..H005: same timestamp `1778300987634`, value `N 240`, `reasonCount=0`
    - H006: same timestamp `1778300987634`, value `N 240`, `reasonCount=2`
    - subsequent blocks also contain repeated timestamps for value `N 0`
- Conclusion from storage evidence: duplicates are already persisted in MessageStore data; duplication is not introduced only by HTTP GET rendering.

## latest field evidence (light on time)
- Additional files provided by user:
    - `spec/bug/msgstore-many-entries-from-single-message/yahamsgstoreclient_light_on_time_latest.txt`
    - `spec/bug/msgstore-many-entries-from-single-message/http_result.log`
- Input log file (`yahamsgstoreclient_light_on_time_latest.txt`) shows repeated receive events every ~10 seconds for:
    - `first/study/main/light/light on time/set`
    - `first/study/main/light/light on time`
- HTTP result file (`http_result.log`) shows newest entries for `first/study/main/light/light on time` with repeated identical timestamp `2026-05-09T05:16:43.000Z` for value `off` in current value and first history rows.
- In same HTTP payload, newest reason chain includes `Request by browser` timestamp `2026-05-09T07:16:43+02:00` and later processing timestamps, while newest history rows still keep the old repeated time.
- Updated symptom interpretation:
    - issue is not only duplicate count
    - incoming newer messages are persisted with a stale/old effective timestamp
    - this makes newer messages look like old duplicated messages in history output

## scope
Allow list:
- Reproduction and analysis only for the path from yahabrokerconnector client input to msgstore client GET output.
- Focus modules: src/yaha/message_store_client and src/yaha/message_store.

Deny list:
- No analysis outside this path.
- No broad checks of unrelated packet types, QoS paths, or unrelated modules.

## root cause and original JS comparison
- Original JS behavior in `spec/@mangar2/messagetree/messagetree.js` (`addData`):
    - node `time` is always assigned from the runtime clock (`cur.getTime()`)
    - reason timestamps are stored as metadata only
- Rebuild C++ behavior before fix in `src/yaha/message_store/message_tree.cpp` (`MessageTree::addData`):
    - node `timeMs` was overwritten by `message.reason().front().timestamp` when parseable
    - if front reason timestamp stayed stale (for example browser request timestamp reused), new updates were projected with old time
- Functional difference between original and rebuild:
    - original JS: `node time = receive time`
    - rebuild before fix: `node time = first reason timestamp (if parseable)`
    - this semantic change created the observed input/output mismatch perception (new updates looked like repeated old entries)

## implemented fix
- File changed: `src/yaha/message_store/message_tree.cpp`
- `MessageTree::addData` now always uses `nowMillisecondsProvider()` for node `timeMs`.
- Reason timestamps remain preserved in `reason[]` for traceability, but do not control node chronology.

## verification after fix
- Unit regression:
    - command: `./build/debug/yahabroker-tests "history_stale_first_reason_timestamp_does_not_collapse_new_updates" --reporter compact --colour-mode none`
    - result: PASS
- Affected addData subset:
    - command: `./build/debug/yahabroker-tests "add_data_*" --reporter compact --colour-mode none`
    - result: PASS
- Field-log integration replay:
    - command: `python3 test/run_integration_tests.py --filter robustness/msgstore_replay_light_on_time_detects_stale_timestamp_projection --timeout 12`
    - result: PASS (`input_messages=9, unique_projected_times=9`)
