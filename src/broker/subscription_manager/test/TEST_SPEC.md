# TEST_SPEC.md — subscription_manager (Module 19)

All tests are tagged [subscription_manager].

## subscription_orchestrator_test.cpp

| Test case | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `subscribe_regular_filter_stores_subscription_and_grants_qos` | Accept non-shared subscription | Filter `sensors/+/temp`, QoS 1, ACL allow | SUBACK reason `GrantedQoS1`; entry stored in SubscriptionStore |
| `subscribe_shared_filter_registers_member_only_in_shared_dispatcher` | Accept shared subscription | Filter `$share/group-a/devices/status`, ACL allow | SUBACK `Success`; member_count for group/filter increments; no regular store entry |
| `subscribe_invalid_shared_syntax_returns_topic_filter_invalid` | Reject malformed shared syntax | Filter `$share/group-a` | SUBACK reason `TopicFilterInvalid` |
| `subscribe_shared_with_no_local_throws_protocol_error` | Enforce No Local protocol constraint for shared | Filter `$share/group-a/devices/status`, no_local=true | `runtime_error` thrown |
| `subscribe_with_identifier_zero_throws_protocol_error` | Enforce Subscription Identifier > 0 | SUBSCRIBE property `SubscriptionIdentifier=0` | `runtime_error` thrown |
| `subscribe_denied_by_acl_returns_not_authorized` | ACL deny path | ACL deny subscribe, filter `private/topic` | SUBACK reason `NotAuthorized`; no store entry |
| `subscribe_invalid_topic_filter_returns_topic_filter_invalid` | Validate topic filter syntax | Filter `a/#/b` | SUBACK reason `TopicFilterInvalid` |
| `subscribe_retain_handling_send_if_new_delivers_only_once` | Retained delivery only for new subscription | Retained message on `alerts/high`; subscribe twice with retain_handling=1 | First subscribe delivers retained once; second subscribe delivers none |
| `subscribe_regular_filter_updates_session_snapshot` | Synchronize durable session snapshot on successful regular subscribe | Existing session for `client-a`; subscribe `snapshot/topic` QoS 1 | SessionState contains one subscription with topic `snapshot/topic` and QoS 1 |
| `unsubscribe_regular_filter_returns_success_then_no_subscription_found` | Remove regular subscription and missing path | Subscribe then unsubscribe same filter twice | First UNSUBACK `Success`; second `NoSubscriptionFound` |
| `unsubscribe_shared_filter_returns_success_then_no_subscription_found` | Remove shared member and missing path | Subscribe shared then unsubscribe same shared filter twice | First UNSUBACK `Success`; second `NoSubscriptionFound` |
| `unsubscribe_invalid_shared_syntax_returns_topic_filter_invalid` | Reject malformed shared filter in unsubscribe | Topic filter `$share/group-a` | UNSUBACK reason `TopicFilterInvalid` |
| `unsubscribe_regular_filter_removes_session_snapshot_subscription` | Synchronize durable session snapshot on successful regular unsubscribe | Existing session with `devices/+/status`; unsubscribe same filter | SessionState no longer contains that subscription |
