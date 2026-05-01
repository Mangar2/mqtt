# broker_connector phase 2-3 test specification

## Scope

Unit tests for source HTTP adapter, source lifecycle manager, receiver publish port, and relay component in phases 2-3.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `source_adapter_connect_subscribe_and_callback_publish` | Adapter performs connect+subscribe and emits inbound publish callback | fake source broker with connect/sub/ping/disconnect endpoints; adapter listener port dynamic; inbound qos1 publish callback | connect and subscribe called once, callback invoked once with topic/value/meta, response returns 204 with packet=puback |
| `source_adapter_qos2_publish_and_pubrel_ack_sequence` | Adapter callback listener returns qos2 ack sequence | inbound qos2 publish then pubrel with packetid | publish response has packet=pubrec and packetid echo, pubrel response has packet=pubcomp and packetid echo |
| `source_lifecycle_retries_connect_and_replays_subscribe` | Lifecycle manager retries after initial connect failure and eventually runs keepalive pings | fake source broker fails first connect, then succeeds | multiple connect attempts, subscribe occurs after successful connect, pingreq called while connected |
| `source_adapter_qos0_publish_with_dup_retain_flags` | Adapter handles qos0 callback with dup/retain true and non-numeric packetid | inbound qos0 publish callback with `dup=1`, `retain=1`, `packetid=abc` | response is 204 with no packet ack header, callback meta has qos0 retain=true dup=true and no packetId |
| `source_adapter_invalid_publish_payload_returns_400` | Adapter rejects malformed callback payload | inbound publish payload missing required topic/value shape | response status 400 and callback is not invoked |
| `source_adapter_ping_when_disconnected_returns_false` | Ping guard branch when no active source session | call ping before connect | returns false with error text and keeps disconnected state |
| `source_adapter_connect_fails_when_broker_unreachable` | Adapter surfaces connect transport failure path | source host/port unreachable | connectAndSubscribe returns false with connect-request failure error |
| `source_adapter_subscribe_status_error_propagates` | Adapter surfaces subscribe status error | fake source broker returns non-200 from `/subscribe` | connectAndSubscribe returns false with subscribe status error |
| `source_adapter_ping_wrong_packet_sets_disconnected` | Adapter handles bad ping response packet | fake source broker returns 204 with packet!=pingresp | ping returns false and adapter transitions to disconnected |
| `receiver_publish_port_start_publish_and_close` | Receiver port starts standard client runtime and forwards publish | fake transport connects successfully and records publish messages | start succeeds, connected state true, publish delegated with mapped qos/retain, close disconnects |
| `receiver_publish_port_disconnected_publish_returns_false` | Receiver port reports publish error while client is disconnected | fake transport always fails connect | publish returns false with receiver publish failure text |
| `receiver_publish_port_publish_before_start_returns_false` | Receiver port rejects publish when runtime was never started | freshly constructed receiver port, publish called directly | publish returns false with runtime-not-started error |
| `receiver_publish_port_start_is_idempotent_and_preserves_reason` | Receiver port handles repeated start and preserves reason chain on mapped message | fake transport connects; start called twice; message contains reason entries | both starts succeed and published message keeps reason entries |
| `relay_component_forwards_message_with_mapped_options` | Relay component forwards one source message with default qos mapping | running component with wired fake receiver port and qos2 source meta | one publish call with outgoing qos1 retain passthrough, counters received=1 forwarded=1 failed=0 |
| `relay_component_retries_then_succeeds` | Relay component retries failed publishes until success | fake receiver publish results false,false,true and retry budget 2 | onIncomingPublish returns true, publish called 3 times, forwarded counter increments |
| `relay_component_counts_failed_after_retry_budget` | Relay component stops after retry budget and counts failure | fake receiver publish results false,false,false and retry budget 2 | onIncomingPublish returns false, publish called 3 times, failed counter increments |
| `relay_component_rejects_when_not_running` | Relay component guard branch for inactive runtime | component wired but not started | onIncomingPublish returns false and counters remain unchanged |
| `relay_component_supports_passthrough_qos_with_backoff` | Relay component executes non-normalized qos branch and backoff sleep path | normalizeQosToAtLeastOnce=false, retainPassthrough=false, first publish fails then succeeds | second attempt succeeds with qos passthrough and retain=false |
