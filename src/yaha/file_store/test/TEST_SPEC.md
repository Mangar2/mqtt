# file_store tests

## Scope

Unit tests for `FileStore` HTTP behavior, key mapping, lifecycle, and MQTT monitoring publish behavior.

## Planned tests

| Test name | Scenario | Input | Expected |
|---|---|---|---|
| `encode_key_path_to_filename_is_deterministic` | Mapping contract from key path to filename | `/a` | result `4797` |
| `get_subscriptions_returns_empty_map` | FileStore has no inbound subscriptions | default config | empty map |
| `http_post_and_get_roundtrip_text_payload` | Text payload path | POST text then GET same key | GET body is JSON string |
| `http_post_and_get_roundtrip_json_payload` | JSON payload path | POST application/json object then GET | GET body equals JSON object text |
| `http_post_rejects_key_longer_than_limit` | Key length guard | POST with key length > max | status 400 |
| `http_post_emits_monitoring_changed_event` | Monitoring publish on successful write | set callback + POST | one `$MONITOR/FileStore/changed` publish |
| `run_and_close_are_idempotent` | Lifecycle reentry safety | run twice, close twice | no crash, running flag toggles correctly |
