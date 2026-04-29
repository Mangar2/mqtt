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
| `close_stops_loop_and_disconnects` | Graceful shutdown stops worker and disconnects | running client then close | `isRunning()==false`, disconnect called once |
