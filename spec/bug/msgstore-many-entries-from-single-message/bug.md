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

## scope
Allow list:
- Reproduction and analysis only for the path from yahabrokerconnector client input to msgstore client GET output.
- Focus modules: src/yaha/message_store_client and src/yaha/message_store.

Deny list:
- No analysis outside this path.
- No broad checks of unrelated packet types, QoS paths, or unrelated modules.
