# TODO: topic/client_cannot_publish_to_sys

Integration test reference: topic/client_cannot_publish_to_sys (spec/integration-test-plan.md §3.4.4)
Test file: test/integration_tests/topic/topic_matching.py

## Problem
Client publishes to `$SYS/broker/clients/connected`; a `$SYS/#` subscriber receives the message.
MQTT 5.0 §4.7.2 says servers SHOULD prevent clients from using $SYS topic names to exchange messages.
The broker routes the message instead of silently dropping it.

## Action
fix broker $SYS publish guard — drop or reject client PUBLISH packets whose topic starts with `$SYS/`
