# TEST_SPEC.md — yaha/zwave

All tests are tagged `[zwave_service]`.

## zwave_service_component_test.cpp — service routing, publish, and lifecycle

| Test case | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `subscriptions_include_management_and_device_topics` | Derive management + device subscriptions from config | one class-bound and one class-free device mapping | management topics present with QoS 2; mapped topics use configured subscribe QoS and wildcard rule |
| `management_messages_are_forwarded_and_scan_success_is_published` | Inbound management topics are routed to controller and successful scan emits notification | removefailed/addnode/scan messages | controller receives matching operations; publish emits `scan command accepted` with configured qos/retain |
| `scan_failure_publishes_error_message` | Scan exception path emits deterministic error publish | scan command with controller throwing exception | publish emits `$MONITOR/zwave/error` value `scan command failed` with exception reason |
| `scan_unknown_failure_publishes_error_message` | Scan unknown-exception path emits deterministic error publish | scan command with controller throwing non-std exception | publish emits `$MONITOR/zwave/error` value `scan command failed` with reason `unknown` |
| `publish_without_callback_logs_error` | Missing publish callback must be observable | startup run without callback | deterministic `zwave_service[error] op=publish reason=callback_missing` log |
| `publish_failure_result_logs_error` | Publish rejection branch must be observable | callback returns failed `PublishResult` during startup publish | deterministic `zwave_service[error] op=publish reason=publish_rejected` log with category/reason |
| `publish_callback_exception_logs_error` | Publish exception branch must be observable | callback throws during startup publish | deterministic `zwave_service[error] op=publish reason=exception` log |
| `publish_callback_unknown_exception_logs_error` | Publish unknown-exception branch must be observable | callback throws non-std exception during startup publish | deterministic `zwave_service[error] op=publish reason=exception` log with unknown detail |
| `logging_flags_emit_incoming_and_outgoing_lines` | Optional inbound/outbound trace logs are controlled by config flags | config with `logIncomingMessages=true` and `logOutgoingMessages=true`, one inbound set and one outbound controller publish | stdout contains one shared log line with `component="zwave_service" direction="incoming"` and one with `component="zwave_service" direction="outgoing"` |
| `log_level_one_emits_important_events_without_forcing_message_traces` | log level 1 emits important events but does not force MQTT trace logs | config with `logLevel=1` and both trace flags disabled, startup + addnode management message | stdout contains `zwave_service[event]` lines for startup/addnode and contains no shared message logs with `component="zwave_service" direction="incoming|outgoing"` |
| `regular_set_message_forwards_reasons_and_publish_flags` | Service forwards incoming reasons to controller and enforces publish flags | inbound `/set` message + controller reply message | controller receives forwarded reasons including `received by zwave service`; outbound keeps configured qos/retain and controller reasons |
| `regular_set_message_value_mismatch_skips_reason_merge` | Service does not perform value-based reason merging | inbound `/set` with `on`, controller publish with `off` | outbound keeps controller reason only and omits received reason |
| `regular_set_message_invalid_received_timestamp_skips_reason_merge` | Service timestamp content does not trigger reason merge behavior | inbound reason timestamp malformed, controller publish timestamp valid | outbound keeps controller reason only and omits received reason |
| `regular_set_message_out_of_window_timestamp_skips_reason_merge` | Service does not merge reasons based on timestamp windows | inbound and controller timestamps differ by >30s | outbound keeps controller reason only and omits received reason |
| `remove_failed_exception_publishes_error_message` | removefailed controller exception containment | removefailed command with controller throwing | deterministic `$MONITOR/zwave/error` publish with operation reason |
| `remove_failed_unknown_exception_publishes_error_message` | removefailed unknown-exception containment | removefailed command with non-std throw | deterministic `$MONITOR/zwave/error` publish with operation reason and unknown detail |
| `add_node_exception_publishes_error_message` | addnode controller exception containment | addnode command with controller throwing | deterministic `$MONITOR/zwave/error` publish with operation reason |
| `add_node_unknown_exception_publishes_error_message` | addnode unknown-exception containment | addnode command with non-std throw | deterministic `$MONITOR/zwave/error` publish with operation reason and unknown detail |
| `set_value_exception_publishes_error_message` | setValue controller exception containment | regular `/set` with controller throwing | deterministic `$MONITOR/zwave/error` publish with operation reason |
| `set_value_unknown_exception_publishes_error_message` | setValue unknown-exception containment | regular `/set` with non-std throw | deterministic `$MONITOR/zwave/error` publish with operation reason and unknown detail |
| `run_publishes_startup_markers_and_requests_controller_sync` | Startup run flow markers and config sync | `run()` call | publishes `removefailednode` + `addnode` restart markers and requests config params once |
| `run_request_config_exception_publishes_error_message` | run request-config exception containment | `run()` with controller request throwing | deterministic `$MONITOR/zwave/error` publish with operation reason |
| `run_request_config_unknown_exception_publishes_error_message` | run request-config unknown-exception containment | `run()` with non-std throw from controller request | deterministic `$MONITOR/zwave/error` publish with operation reason and unknown detail |
| `close_delegates_to_controller` | Close flow delegation | `close()` call | controller close invoked once |
| `close_exception_publishes_error_message` | close exception containment | `close()` with controller throwing | deterministic `$MONITOR/zwave/error` publish with operation reason |
| `close_unknown_exception_publishes_error_message` | close unknown-exception containment | `close()` with non-std throw | deterministic `$MONITOR/zwave/error` publish with operation reason and unknown detail |
