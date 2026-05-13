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
| `http_get_rejects_key_longer_than_limit` | Key length guard for read path | GET with key length > max | status 400 |
| `http_get_missing_key_returns_error` | Missing key read path | GET for not existing key | status 404 with YahaError payload code `YAHA_FILE_STORE_KEY_NOT_FOUND` |
| `http_post_invalid_json_returns_error` | Invalid JSON rejected in JSON mode | POST with malformed JSON and content-type application/json | status 400 with YahaError payload code `YAHA_FILE_STORE_INVALID_JSON_PAYLOAD` |
| `http_post_invalid_json_token_returns_error` | Invalid JSON first token rejected | POST with malformed JSON token and content-type application/json | status 400 with YahaError payload code `YAHA_FILE_STORE_INVALID_JSON_PAYLOAD` |
| `http_options_returns_cors_headers` | CORS preflight contract | OPTIONS request | status 200 and CORS headers |
| `http_roundtrip_text_payload_escapes_json_control_chars` | Text payload escaping in GET response | POST text containing quote, backslash, newline, carriage return, tab then GET | GET body contains escaped JSON string |
| `http_get_reads_legacy_untyped_payload_file` | Backward compatibility for old file format without type prefix | manually write plain file content then GET | JSON string body returned |
| `monitoring_disabled_suppresses_post_event` | Monitoring disable switch | monitoring.enabled=false and successful POST | no publish callback event |
| `monitoring_error_event_is_emitted_for_invalid_directory` | Watcher snapshot error publishes monitoring error with details | configure directory path as regular file and run watcher | one `$MONITOR/FileStore/error` event with payload details |
| `monitoring_topic_prefix_all_slashes_uses_suffix_topic` | Empty normalized topic prefix path | monitoring.topicPrefix set to only slashes and successful POST | published topic equals event suffix |
| `monitoring_topic_prefix_empty_uses_default_prefix` | Default topic prefix fallback path | monitoring.topicPrefix set to empty and successful POST | published topic starts with `$MONITOR/FileStore/` |
| `http_post_returns_error_when_directory_is_not_writable_directory` | Write failure path bubbles to HTTP server error | configure directory path as regular file and POST | status 500 with YahaError payload code `YAHA_FILE_STORE_PERSIST_FAILED` and persistence details |
| `run_invokes_http_start_and_stop_callbacks` | HTTP lifecycle callbacks from config | set start/stop callbacks and run+close | both callbacks invoked exactly once |
| `handle_message_is_noop` | Inbound message handler contract | call handleMessage with any message | no throw and component stays usable |
| `http_post_emits_monitoring_changed_event` | Monitoring publish on successful write | set callback + POST | one `$MONITOR/FileStore/changed` publish |
| `watcher_emits_created_changed_deleted_events` | Filesystem watcher emits events for out-of-band file changes | create, update, delete file in store dir | changed + deleted topics published (created may be timing-dependent) |
| `watcher_filesystem_event_includes_key_path_for_known_file` | Watcher event includes resolved key path for known persisted file | POST key to create known mapping, then modify mapped file out-of-band | filesystem event (`changed` or race-equivalent `created`) contains source `filesystem-watch` and `keyPath` for the key |
| `run_and_close_are_idempotent` | Lifecycle reentry safety | run twice, close twice | no crash, running flag toggles correctly |
| `monitoring_publish_throw_logs_out_fail_without_false_success` | Monitoring callback throw must never emit success outbound log | callback throws and HTTP POST triggers monitoring publish | logs contain `file_store[out-fail]` and no matching `file_store[out]` for that event |
| `monitoring_publish_result_failure_logs_structured_category` | Explicit callback failure result must be logged with category | callback returns `PublishResult::fail(AckTimeout)` and HTTP POST triggers monitoring publish | logs contain `category=ack_timeout` and `file_store[out-fail]` |
| `monitoring_retry_queue_flushes_after_callback_restore` | Queued monitoring event is sent after callback becomes available | first event queued because callback missing, callback set later, activity triggers retry | queued monitoring publish reaches callback successfully |
| `monitoring_retry_exhaustion_logs_retry_exhausted` | Retry queue stops after bounded budget and emits final error | callback always throws, one event queued, repeated activity drives retries | logs contain `category=retry_exhausted` |
