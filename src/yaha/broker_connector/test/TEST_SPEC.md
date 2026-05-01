# broker_connector phase 2 test specification

## Scope

Unit tests for source HTTP adapter and source lifecycle manager in phase 2.

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
