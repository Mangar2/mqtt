# http_mqtt_interface_client integration test specification

## Scope

Integration tests for the YAHA HTTP MQTT service using the original TypeScript client sources from `spec/@mangar2/mqtt-client`.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `yaha/http_mqtt_interface_client/typescript_full_flow` | End-to-end HTTP MQTT protocol flow executed by original TypeScript client code against real YAHA broker backend | Start `yahabroker` and `yahahttpmqttinterfaceclient`, then run TypeScript flow (connect, subscribe, publish QoS 0/1/2, ping, unsubscribe, disconnect) | TypeScript flow process exits successfully, receives own published payloads, and reports successful command sequence |
