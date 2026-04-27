# client_api/test — Unit tests for synchronous client API (Step 23)

## SyncClient

| Test case | Tag | Description |
|-----------|-----|-------------|
| `sync_client_connect_uses_callback_and_marks_connected` | `[client_api][sync]` | `connect()` forwards timeout to callback and transitions client to connected state. |
| `sync_client_publish_qos0_sends_once_and_returns_success` | `[client_api][sync]` | QoS0 publish sends one packet and returns success without ACK wait. |
| `sync_client_publish_qos2_runs_pubrec_pubrel_pubcomp_sequence` | `[client_api][sync]` | QoS2 publish executes full blocking ACK path and returns final reason code. |
| `sync_client_subscribe_and_unsubscribe_roundtrip_updates_active_filters` | `[client_api][sync]` | Subscribe and unsubscribe operations return ACK reasons and update active subscription state. |
| `sync_client_publish_qos1_requires_puback_callback` | `[client_api][sync]` | QoS1 publish without wait_puback callback raises timeout-classified client exception. |
| `sync_client_operations_require_connected_state` | `[client_api][sync]` | publish/subscribe/unsubscribe reject calls while disconnected. |
| `sync_client_rejects_empty_client_id` | `[client_api][sync]` | Constructing with empty client id fails with invalid-packet client exception. |
| `sync_client_connect_requires_connect_callback` | `[client_api][sync]` | connect fails when connect callback is missing. |
| `sync_client_publish_qos2_error_pubrec_finishes_without_pubrel` | `[client_api][sync]` | QoS2 publish exits early on error PUBREC and does not send PUBREL/PUBCOMP. |
| `sync_client_subscribe_unsubscribe_rejected_reasons_keep_state_stable` | `[client_api][sync]` | Rejected SUBACK/UNSUBACK reason codes do not mutate active subscription state. |
| `sync_client_disconnect_without_callback_and_already_disconnected_is_tolerated` | `[client_api][sync]` | disconnect works without send callback and is idempotent when already disconnected. |

## AsyncClient (Step 24)

| Test case | Tag | Description |
|-----------|-----|-------------|
| `async_client_connect_completes_on_dispatch_thread` | `[client_api][async]` | Non-blocking connect completion is reported on internal dispatch thread and sets connected state. |
| `async_client_publish_qos0_invokes_completion_and_send_callback` | `[client_api][async]` | Non-blocking QoS0 publish calls send callback once and reports success. |
| `async_client_subscribe_then_inbound_publish_calls_message_handler` | `[client_api][async]` | Subscribed inbound publish is forwarded to registered message handler on dispatch thread. |
| `async_client_unsubscribe_reports_error_result_without_state_regression` | `[client_api][async]` | Unsubscribe completion returns broker error reason while keeping active subscription state. |
| `async_client_publish_without_connect_maps_client_exception_to_async_error` | `[client_api][async]` | Operation failure returns `AsyncOperationError` payload instead of throwing. |
