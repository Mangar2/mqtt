# client/test — Unit Tests for client module

## KeepAliveManager (Step 16)

| Test case | Tag | Description |
|-----------|-----|-------------|
| `keep_alive_disabled_emits_no_action` | `[client][keep_alive]` | KeepAlive `0` never emits `SendPingreq` or `Timeout`. |
| `keep_alive_emits_pingreq_after_idle_interval` | `[client][keep_alive]` | After idle interval elapses, one `SendPingreq` is emitted and manager waits for `PINGRESP`. |
| `keep_alive_timeout_when_pingresp_missing` | `[client][keep_alive]` | Missing `PINGRESP` after response timeout emits `Timeout`. |
| `keep_alive_pingresp_clears_pending_state` | `[client][keep_alive]` | `on_pingresp()` clears pending timeout and restarts idle timer. |

## OutboundTopicAliasManager (Step 17)

| Test case | Tag | Description |
|-----------|-----|-------------|
| `alias_manager_disabled_does_not_modify_packet` | `[client][alias]` | `max_aliases=0` leaves outbound packet unchanged and returns false. |
| `alias_manager_first_publish_assigns_alias_and_keeps_topic` | `[client][alias]` | First publish gets TopicAlias property and still carries full topic name. |
| `alias_manager_repeated_publish_reuses_alias_and_clears_topic` | `[client][alias]` | Second publish of same topic reuses alias and strips topic string. |
| `alias_manager_rejects_empty_topic` | `[client][alias]` | Empty-topic outbound packet throws `ClientException(InvalidPacket)`. |
| `alias_manager_respects_reset_and_reports_maximum` | `[client][alias]` | `reset()` clears mappings and alias allocation restarts from 1; `max_aliases()` reports configured value. |
| `alias_manager_updates_existing_topic_alias_property` | `[client][alias]` | Existing TopicAlias property is updated in-place on repeated publish. |
| `alias_manager_reuses_alias_when_capacity_is_full` | `[client][alias]` | With full alias space, new topics reuse an existing alias and evict prior mapping. |

## ConnectionNegotiator (Step 18)

| Test case | Tag | Description |
|-----------|-----|-------------|
| `connection_negotiator_dial_tcp_invalid_host_throws` | `[client][negotiator]` | DNS failure during `dial_tcp` throws `ResolveFailed`. |
| `connection_negotiator_successfully_parses_connack` | `[client][negotiator]` | CONNECT/CONNACK exchange returns negotiated values (`session_present`, limits, assigned id). |
| `connection_negotiator_rejected_connack_throws` | `[client][negotiator]` | Error CONNACK reason throws `NegotiationRejected` with reason code. |
| `connection_negotiator_non_connack_response_throws_protocol_error` | `[client][negotiator]` | First response packet not CONNACK throws `ProtocolError`. |
| `connection_negotiator_dial_tcp_local_listener_success` | `[client][negotiator]` | `dial_tcp` succeeds against a local ephemeral listener. |
| `connection_negotiator_dial_tcp_connect_failure_throws` | `[client][negotiator]` | Name resolves but no endpoint accepts connection -> `ConnectFailed`. |
| `connection_negotiator_write_failure_throws` | `[client][negotiator]` | Invalid socket write while sending CONNECT throws `WriteFailed`. |
| `connection_negotiator_peer_close_before_connack_throws` | `[client][negotiator]` | Peer closes without CONNACK -> `ReadFailed`. |
| `connection_negotiator_timeout_waiting_for_connack_throws` | `[client][negotiator]` | No response until timeout -> `Timeout`. |

## ClientSessionStateKeeper (Step 19)

| Test case | Tag | Description |
|-----------|-----|-------------|
| `session_state_keeper_upsert_and_remove_subscriptions` | `[client][state_keeper]` | Upsert replaces same topic filter, remove returns expected flags, clear removes all. |
| `session_state_keeper_restore_plan_respects_clean_start` | `[client][state_keeper]` | `build_restore_plan(true)` returns empty; `build_restore_plan(false)` returns stored subscriptions/inflight/expiry. |
| `session_state_keeper_set_outbound_inflight_filters_and_sorts` | `[client][state_keeper]` | Keeps only outbound non-zero packet IDs and sorts by timestamp then packet_id. |
| `session_state_keeper_capture_outbound_inflight_from_store` | `[client][state_keeper]` | Captures only outbound entries for owning client from `InflightStore`. |
| `session_state_keeper_snapshot_roundtrip_and_mismatch_guard` | `[client][state_keeper]` | Snapshot apply roundtrip works for same client_id and throws for mismatched client_id. |

## ClientSubscriptionManager (Step 20)

| Test case | Tag | Description |
|-----------|-----|-------------|
| `subscription_manager_begin_subscribe_builds_packet_and_activates_on_suback` | `[client][subscription]` | SUBSCRIBE packet gets packet id and accepted SUBACK activates filter callback. |
| `subscription_manager_suback_reject_keeps_filter_inactive` | `[client][subscription]` | Error SUBACK reason does not activate local subscription callback. |
| `subscription_manager_begin_unsubscribe_and_unsuback_remove_filter` | `[client][subscription]` | UNSUBSCRIBE/UNSUBACK flow removes previously active filter on success. |
| `subscription_manager_suback_unknown_packet_id_throws` | `[client][subscription]` | Unknown SUBACK packet id is rejected as protocol error. |
| `subscription_manager_reason_count_mismatch_throws` | `[client][subscription]` | ACK reason code count mismatch against pending request throws protocol error. |
| `subscription_manager_validates_filters_and_topic_name` | `[client][subscription]` | Invalid subscribe/unsubscribe filters and invalid inbound publish topic are rejected. |
