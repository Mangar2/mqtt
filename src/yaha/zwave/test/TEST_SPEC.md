# TEST_SPEC.md — yaha/zwave

All tests are tagged `[zwave_service]`.

## zwave_service_component_test.cpp — service routing, publish, and lifecycle

| Test case | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `subscriptions_include_management_and_device_topics` | Derive management + device subscriptions from config | one class-bound and one class-free device mapping | management topics present with QoS 2; mapped topics use configured subscribe QoS and wildcard rule |
| `management_messages_are_forwarded_and_scan_success_is_published` | Inbound management topics are routed to controller and successful scan emits notification | removefailed/addnode/scan messages | controller receives matching operations; publish emits `scan command accepted` with configured qos/retain |
| `scan_failure_publishes_error_message` | Scan exception path emits deterministic error publish | scan command with controller throwing exception | publish emits `$MONITORING/zwave/error` value `scan command failed` with exception reason |
| `regular_set_message_updates_matcher_and_publish_flags` | Reply matcher merges reasons and enforces publish flags | inbound `/set` message + controller reply message | outbound message keeps configured qos/retain and contains both inbound + controller reasons |
| `run_publishes_startup_markers_and_requests_controller_sync` | Startup run flow markers and config sync | `run()` call | publishes `removefailednode` + `addnode` restart markers and requests config params once |
| `close_delegates_to_controller` | Close flow delegation | `close()` call | controller close invoked once |
