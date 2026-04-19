# TEST_SPEC — message_router (Module 12)

| Test case | Module | Description |
|-----------|--------|-------------|
| `inbound_processor_auth_denied_throws`         | 12.1.2 | PublishNotAuthorized exception when ACL denies |
| `inbound_processor_no_subscribers`             | 12.1.4 | Empty result when no subscriptions match |
| `inbound_processor_returns_subscribers`        | 12.1.4 | Matching subscriber returned |
| `inbound_processor_stores_retained`            | 12.1.3 | Retained message written to store when retain=true |
| `inbound_processor_no_retain_when_flag_false`  | 12.1.3 | Nothing stored when retain=false |
| `inbound_processor_alias_new_registration`     | 12.1.1 | Topic + alias registers new mapping; alias property stripped |
| `inbound_processor_alias_only_resolution`      | 12.1.1 | Empty topic resolved via existing alias |
| `inbound_processor_alias_unregistered_throws`  | 12.1.1 | TopicAliasInvalid when alias not registered |
| `inbound_processor_alias_out_of_range_throws`  | 12.1.1 | TopicAliasInvalid when alias exceeds maximum |
| `fanout_no_local_filters_publisher`            | 12.2.2 | Subscriber omitted when no_local=true and publisher==subscriber |
| `fanout_no_local_false_includes_publisher`     | 12.2.2 | Subscriber included when no_local=false |
| `fanout_qos_downgrade`                         | 12.2.1 | Outbound QoS capped at subscription QoS |
| `fanout_qos_not_upgraded`                      | 12.2.1 | Publisher QoS not increased beyond subscription QoS |
| `fanout_retain_cleared_by_default`             | 12.2.3 | RETAIN flag cleared when retain_as_published=false |
| `fanout_retain_preserved_when_option_set`      | 12.2.3 | RETAIN flag preserved when retain_as_published=true |
| `fanout_subscription_identifier_attached`      | 12.2.4 | SubscriptionIdentifier property added when set on subscription |
| `fanout_no_subscription_identifier_when_absent`| 12.2.4 | No SubscriptionIdentifier property when subscription has none |
| `fanout_multiple_subscribers`                  | 12.2   | Two distinct subscribers produce two DeliveryItems |
| `offline_queue_enqueue_and_drain`              | 12.3.1/2 | Message enqueued and drained correctly |
| `offline_queue_drain_empty_client`             | 12.3.2 | Drain of unknown client returns empty vector |
| `offline_queue_exceeds_limit_throws`           | 12.3.3 | QueueFull exception when limit exceeded |
| `offline_queue_drain_preserves_fifo_order`     | 12.3.2 | Messages delivered in FIFO order |
| `offline_queue_purge_removes_messages`         | 12.3   | Purge clears all messages without delivering |
| `offline_queue_size_per_client`                | 12.3   | Independent per-client size tracking |
| `offline_queue_enqueue_drop_oldest_replaces_head_when_full` | 12.3.3 | Drop-oldest enqueue keeps newest messages when queue is full |
| `offline_queue_enqueue_drop_oldest_handles_zero_capacity` | 12.3.3 | Drop-oldest enqueue remains stable for zero configured capacity |
| `expiry_no_property_always_valid`              | 12.4   | No MessageExpiryInterval → always valid |
| `expiry_not_expired_updates_remaining`         | 12.4.3 | Remaining interval reduced correctly after elapsed time |
| `expiry_exactly_at_boundary_expired`           | 12.4.2 | Expired when elapsed == interval |
| `expiry_past_boundary_expired`                 | 12.4.2 | Expired when elapsed > interval |
| `expiry_zero_elapsed_unchanged`                | 12.4.3 | Zero elapsed leaves interval unchanged |
| `shared_single_member_always_selected`         | 12.5.2 | Single member consistently selected |
| `shared_two_members_round_robin`               | 12.5.2 | Two members alternate on successive calls |
| `shared_remove_member_cleans_up_empty_group`   | 12.5.3 | Group deleted when last member removed |
| `shared_remove_client_from_all_groups`         | 12.5.3 | remove_client purges client from every group |
| `shared_no_match_different_topic`              | 12.5.1 | No result when topic does not match filter |
| `shared_wildcard_plus_matches`                 | 12.5.1 | '+' wildcard matches single level |
| `shared_wildcard_hash_matches`                 | 12.5.1 | '#' wildcard matches any remaining levels |
| `shared_system_topic_not_matched_by_wildcard`  | 12.5.1 | '$'-prefix topics excluded from '+' first-level match |
| `shared_multiple_groups_each_gets_one_delivery`| 12.5.2 | One delivery per group for the same topic |
| `shared_member_count_correct`                  | 12.5   | member_count reflects additions and removals |
| `shared_replace_existing_member`               | 12.5   | Re-adding a known client replaces subscription |
| `router_deliver_to_online_subscriber`          | 12     | Message delivered via callback for online subscriber |
| `router_enqueue_for_offline_subscriber`        | 12.3   | QoS1 message buffered in OfflineQueue for offline subscriber |
| `router_flush_delivers_queued_messages`        | 12.3.2 | flush_offline_queue drains and delivers buffered QoS1 messages |
| `router_flush_discards_expired_messages`       | 12.4.2 | Expired queued QoS1 messages not delivered during flush |
| `router_auth_denied_throws`                    | 12.1.2 | route() propagates PublishNotAuthorized exception |
| `router_route_internal_uses_alias_maximum_zero` | 12 | route_internal routes server-originated messages without caller alias table plumbing |
| `router_buffer_offline_messages_enqueues_until_queue_full` | 12.3 | buffer_offline_messages enqueues in order and stops at QueueFull boundary |
| `router_deliver_retained_send_if_new`          | 25.1.2 | RetainHandling::SendIfNew suppresses updates and delivers on new subscription |
| `router_deliver_retained_never`                | 25.1.2 | RetainHandling::Never suppresses retained delivery |
| `router_deliver_retained_discards_zero_expiry` | 25.1.2 | deliver_retained drops retained messages with zero expiry interval |
