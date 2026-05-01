# mqtt_client test specification

## Scope

Unit tests for `YahaMqttClient` session behavior with a fake transport callback bundle.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `run_injects_publish_callback_before_message_delivery` | Component must have callback before first inbound message handling | run + one inbound message | callback present during handle, one message handled |
| `connect_subscribes_all_component_filters` | Initial successful connect subscribes all component topics | component with two subscriptions | two subscribe calls with expected filters/qos |
| `disconnect_triggers_reconnect_and_resubscribe` | Connection loss requires reconnect and subscription replay | connect ok, then forced disconnected, reconnect ok | connect called multiple times, subscriptions replayed |
| `publish_forwards_valid_message_to_transport` | Outbound publish path delegates to transport publish callback | one valid message | transport publish called with same topic/value |
| `inbound_non_matching_topic_is_filtered_out` | Messages outside active filters must not reach component | subscription `home/+/state`, inbound `other/topic` | zero handled messages |
| `keep_alive_ping_runs_while_connected` | Session sends periodic keepalive pings | short keepalive interval | ping callback invoked at least once |
| `close_stops_loop_and_disconnects` | Graceful shutdown stops worker, unsubscribes active filters, and disconnects | running client then close | `isRunning()==false`, unsubscribe called for active filter, disconnect called once |
| `transport_poll_exception_triggers_reconnect_without_crash` | Transport callback exception must not terminate process and should trigger reconnect | one transient exception from `pollIncoming()` while connected | worker keeps running, reconnect occurs, close succeeds |
| `mqtt_client_config_maps_optional_mqtt_fields` | Parse [mqtt] section into config | complete [mqtt] section | config fields reflect parsed values |
| `mqtt_client_config_rejects_invalid_numeric_values` | Validate [mqtt] numeric fields | `port = abc` | parser fails with `invalid mqtt.port` |
| `mqtt_client_subscription_parser_reads_topic_qos_map` | Parse [subscriptions] entries | `topic=qos` pairs | map contains all topic/qos entries |
| `mqtt_client_subscription_parser_rejects_invalid_qos` | Validate subscription qos range | qos value `9` | parser fails with invalid qos error |
| `mqtt_client_runtime_run_until_signal_starts_and_stops_component` | Runtime wrapper orchestrates component and client lifecycle | running runtime in background thread, then SIGTERM | component run/close invoked exactly once and mqtt client stops cleanly |
| `broker_transport_connect_poll_publish_and_unsubscribe_roundtrip` | Default broker transport callback bundle performs packet roundtrip with a broker | local fake MQTT broker with connack/suback/unsuback and incoming pingresp/pubrel/publish packets | connect/subscribe/poll/publish/ping/unsubscribe/disconnect complete and incoming values are decoded correctly |
