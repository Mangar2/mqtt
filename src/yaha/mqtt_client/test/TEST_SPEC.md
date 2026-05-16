# mqtt_client test specification

## Scope

Unit tests for `YahaMqttClient` session behavior with a fake transport callback bundle.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `run_injects_publish_callback_before_message_delivery` | Component must have callback before first inbound message handling | run + one inbound message | callback present during handle, one message handled |
| `connect_subscribes_all_component_filters` | Initial successful connect subscribes all component topics | component with two subscriptions | two subscribe calls with expected filters/qos |
| `disconnect_triggers_reconnect_and_resubscribe` | Connection loss requires reconnect and subscription replay | connect ok, then forced disconnected, reconnect ok | connect called multiple times, subscriptions replayed |
| `failed_subscribe_confirmation_keeps_filter_inactive` | Failed broker subscribe confirmation must not activate filter locally | first subscribe succeeds, second subscribe confirmation fails, then message arrives on failed filter | message on failed filter is not delivered to component |
| `runtime_resync_updates_subscription_diff_without_inbound_message` | Runtime must apply component subscription diff even when no inbound message arrives | component returns initial subscriptions first, updated subscriptions on next runtime poll | additional subscription is applied without requiring any inbound message |
| `publish_forwards_valid_message_to_transport` | Outbound publish path delegates to transport publish callback | one valid message | transport publish called with same topic/value |
| `publish_callback_maps_qos1_ack_timeout_to_publish_result` | Publish callback maps QoS1 ACK timeout to explicit result | transport publish throws `timed out waiting for PUBACK from broker` | callback result is failure with category `AckTimeout` and PUBACK reason |
| `publish_callback_maps_qos2_ack_timeout_to_publish_result` | Publish callback maps QoS2 ACK timeout to explicit result | transport publish throws `timed out waiting for PUBREC from broker` | callback result is failure with category `AckTimeout` and PUBREC reason |
| `inbound_non_matching_topic_is_filtered_out` | Messages outside active filters must not reach component | subscription `home/+/state`, inbound `other/topic` | zero handled messages |
| `keep_alive_ping_runs_while_connected` | Session sends periodic keepalive pings | short keepalive interval | ping callback invoked at least once |
| `message_trace_escapes_string_and_formats_numeric_values` | Message trace prints escaped inbound/outbound payload fields and structured reasons | one inbound numeric message and one outbound message with raw payload and two reason entries | trace output includes incoming numeric value, outgoing topic/value, escaped raw payload, and chronological structured reason array with timestamps |
| `close_stops_loop_and_disconnects` | Graceful shutdown stops worker, unsubscribes active filters, and disconnects | running client then close | `isRunning()==false`, unsubscribe called for active filter, disconnect called once |
| `transport_poll_exception_triggers_reconnect_without_crash` | Transport callback exception must not terminate process and should trigger reconnect | one transient exception from `pollIncoming()` while connected | worker keeps running, reconnect occurs, close succeeds |
| `mqtt_client_config_maps_optional_mqtt_fields` | Parse [mqtt] section into config | complete [mqtt] section | config fields reflect parsed values |
| `mqtt_client_config_rejects_invalid_numeric_values` | Validate [mqtt] numeric fields | `port = abc` | parser fails with `invalid mqtt.port` |
| `mqtt_client_config_rejects_invalid_log_reason_value` | Validate [mqtt] reason-log boolean field | `logReason = maybe` | parser fails with `invalid boolean value for 'mqtt.logReason'` |
| `mqtt_client_subscription_parser_reads_topic_qos_map` | Parse [subscriptions] entries | `topic=qos` pairs | map contains all topic/qos entries |
| `mqtt_client_subscription_parser_rejects_invalid_qos` | Validate subscription qos range | qos value `9` | parser fails with invalid qos error |
| `mqtt_client_runtime_run_until_signal_starts_and_stops_component` | Runtime wrapper orchestrates component and client lifecycle | running runtime in background thread, then SIGTERM | component run/close invoked exactly once and mqtt client stops cleanly |
| `broker_transport_connect_poll_publish_and_unsubscribe_roundtrip` | Default broker transport callback bundle performs packet roundtrip with a broker | local fake MQTT broker with connack/suback/unsuback and incoming pingresp/pubrel/publish packets including forwarded JSON payload envelope | connect/subscribe/poll/publish/ping/unsubscribe/disconnect complete, inbound envelope is parsed into internal topic/value/reason while raw payload is preserved unchanged, inbound MQTT DUP is preserved in received Message, and outbound publish uses raw payload bytes unchanged when present including DUP for QoS>0 |
| `broker_transport_parses_forwarded_numeric_and_scalar_reason_payloads` | Forwarded envelope parser supports scalar value and reason variants | forwarded envelope with numeric value + string reason and forwarded envelope with boolean value and omitted reason | numeric value maps to double with reason entry, boolean maps to string and payload remains raw |
| `broker_transport_publish_throws_when_disconnected_other_operations_noop` | Disconnected transport must reject publish explicitly but keep control operations safe | disconnected transport with publish/subscribe/unsubscribe/ping/disconnect calls | publish throws runtime error; remaining operations stay no-op and transport remains disconnected |
| `broker_transport_malformed_forwarded_payloads_fall_back_to_raw_messages` | Malformed forwarded envelope variants must never break polling and must degrade to raw payload messages | broker sends malformed forwarded JSON variants (missing colon, mismatched embedded topic, malformed reason array, invalid scalar reason token) | all payloads are delivered as raw string messages on original MQTT topic without parsed reason metadata |
| `broker_transport_publish_handles_interleaved_ack_packets` | QoS1 publish ack wait should tolerate concurrent control/data packets | broker sends pingresp + publish + pubrel before final puback | publish succeeds and interleaved publish is buffered for subsequent poll |
