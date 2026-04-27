# client_api/test — Unit tests for public client API (Steps 23, 24, 25)

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
| `sync_client_publish_qos2_error_pubrec_finishes_without_pubrel` | `[client_api][sync]` | QoS2 publish converts broker error PUBREC into unified client API exception and does not send PUBREL/PUBCOMP. |
| `sync_client_subscribe_unsubscribe_rejected_reasons_keep_state_stable` | `[client_api][sync]` | Rejected SUBACK/UNSUBACK reason codes are signaled as unified client API exception without mutating active subscription state. |
| `sync_client_disconnect_without_callback_and_already_disconnected_is_tolerated` | `[client_api][sync]` | disconnect works without send callback and is idempotent when already disconnected. |

## AsyncClient (Step 24)

| Test case | Tag | Description |
|-----------|-----|-------------|
| `async_client_connect_completes_on_dispatch_thread` | `[client_api][async]` | Non-blocking connect completion is reported on internal dispatch thread and sets connected state. |
| `async_client_publish_qos0_invokes_completion_and_send_callback` | `[client_api][async]` | Non-blocking QoS0 publish calls send callback once and reports success. |
| `async_client_subscribe_then_inbound_publish_calls_message_handler` | `[client_api][async]` | Subscribed inbound publish is forwarded to registered message handler on dispatch thread. |
| `async_client_unsubscribe_reports_error_result_without_state_regression` | `[client_api][async]` | Unsubscribe completion returns unified error payload for broker rejection while keeping active subscription state. |
| `async_client_publish_without_connect_maps_client_exception_to_async_error` | `[client_api][async]` | Operation failure returns `AsyncOperationError` payload instead of throwing. |
| `async_client_connect_runtime_error_maps_to_async_protocol_error` | `[client_api][async]` | Unexpected non-client exception in connect callback is mapped to protocol-classified async error payload. |
| `async_client_subscribe_without_connect_returns_async_error` | `[client_api][async]` | Non-blocking subscribe reports disconnected-state error via completion callback. |
| `async_client_unsubscribe_without_connect_returns_async_error` | `[client_api][async]` | Non-blocking unsubscribe reports disconnected-state error via completion callback. |
| `async_client_disconnect_is_enqueued_and_prevents_later_publish` | `[client_api][async]` | Enqueued disconnect transitions client to disconnected state before a later queued publish executes. |
| `async_client_default_connect_without_callbacks_reports_error` | `[client_api][async]` | Default-overload async connect without transport callback reports protocol error via unified async payload. |
| `async_client_default_connect_completion_throwing_client_exception_is_converted_to_error_callback` | `[client_api][async]` | Default-overload connect completion throwing ClientException is converted into an async error callback invocation. |
| `async_client_default_connect_completion_throwing_std_exception_is_converted_to_error_callback` | `[client_api][async]` | Default-overload connect completion throwing std::exception is converted into unknown-category async error callback invocation. |
| `async_client_publish_completion_throwing_client_exception_is_converted_to_error_callback` | `[client_api][async]` | Publish completion callback failure with ClientException is caught and converted to unified error callback payload. |
| `async_client_subscribe_completion_throwing_std_exception_is_converted_to_error_callback` | `[client_api][async]` | Subscribe completion callback failure with std::exception is caught and converted to unknown-category error callback payload. |
| `async_client_unsubscribe_completion_throwing_client_exception_is_converted_to_error_callback` | `[client_api][async]` | Unsubscribe completion callback failure with ClientException is caught and converted to unified error callback payload. |
| `async_client_completion_throwing_client_exception_is_converted_to_error_callback` | `[client_api][async]` | Completion callback failure with ClientException is caught and converted to unified error callback payload. |
| `async_client_completion_throwing_std_exception_is_converted_to_error_callback` | `[client_api][async]` | Completion callback failure with std::exception is caught and converted to unknown-category error callback payload. |

## ClientConfig (Step 25)

| Test case | Tag | Description |
|-----------|-----|-------------|
| `client_config_defaults_are_sensible` | `[client_api][config]` | Default configuration values provide valid host, port, client id, and operation timeouts. |
| `client_config_validate_rejects_invalid_values` | `[client_api][config]` | Validation rejects empty host/client id, invalid credentials, zero port, zero receive maximum, and zero operation timeout values. |
| `client_config_build_connect_packet_maps_credentials_and_properties` | `[client_api][config]` | Connect packet builder maps clean-start, keep-alive, credentials, and CONNECT properties from configuration. |
| `sync_client_constructed_from_config_uses_configured_connect_defaults` | `[client_api][config]` | SyncClient created from ClientConfig builds CONNECT packet and uses configured default connect timeout. |
| `async_client_default_overloads_use_configured_timeouts` | `[client_api][config]` | AsyncClient no-timeout overloads use ClientConfig default operation timeouts for enqueue execution. |

## ClientApiError (Step 26)

| Test case | Tag | Description |
|-----------|-----|-------------|
| `client_api_error_from_client_exception_maps_network_timeout_and_protocol` | `[client_api][error]` | Mapping from internal ClientException yields structured categories for network, timeout, and protocol failures. |
| `client_api_error_reason_code_classification_covers_auth_and_authorization` | `[client_api][error]` | Reason-code classification maps authentication and authorization broker failures correctly. |
| `client_api_error_from_std_exception_is_unknown_category` | `[client_api][error]` | Generic std::exception mapping preserves message and marks category as unknown. |
| `client_api_error_mapping_covers_configuration_and_reason_override_paths` | `[client_api][error]` | Internal invalid/configuration errors and reason-code override mapping paths are classified correctly. |
| `sync_client_throws_client_api_exception_for_broker_reason_code_errors` | `[client_api][error]` | Public SyncClient API raises unified exception including broker reason code classification when ACK returns error reason. |
