# serial_device test specification

## Scope

Unit tests for phase-1 subscription derivation contract in serial_device.

## Planned and implemented test cases

1. derive_subscriptions_always_includes_system_topic
- Scenario: empty interface map.
- Expected: output includes $SYS/serialdevice/# with configured QoS.

2. derive_subscriptions_adds_topic_map_topics_with_plus_suffix
- Scenario: switch topic map contains one topic entry.
- Expected: output contains <topic>/+ with configured QoS.

3. derive_subscriptions_without_receiver_map_uses_command_suffix_only
- Scenario: receiverMapProvided is false with commandMap entries.
- Expected: output contains <commandSuffix>/+ entries without receiver prefix.

4. derive_subscriptions_with_receiver_map_uses_prefix_and_suffix
- Scenario: receiverMapProvided is true with receiverMap and commandMap entries.
- Expected: output contains Cartesian product <receiverPrefix><commandSuffix>/+.

5. derive_subscriptions_with_empty_receiver_map_and_provided_flag_adds_no_command_topics
- Scenario: receiverMapProvided is true and receiverMap is empty.
- Expected: output has no commandMap-derived topics and still includes system topic.
