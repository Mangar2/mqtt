# TODO: yaha/http_mqtt_interface_client/typescript_full_flow

Integration test reference: yaha/http_mqtt_interface_client/typescript_full_flow
Test file: test/yaha/http_mqtt_interface_client/typescript_full_flow.py

## Problem
The original TypeScript flow fails at connect with:
`status code 200 expected, got 500`.

Observed behavior in integration run:
- TypeScript client (`spec/@mangar2/mqtt-client`) sends connect payload with `host` and `port` fields.
- HTTP MQTT service returns internal error on `/connect`.
- End-to-end compatibility with original TypeScript connect flow is not yet achieved.

## Action
Implement/fix HTTP MQTT interface connect compatibility handling so original TypeScript connect payload (`host`/`port` semantics) is accepted and completes with a valid connack response.
