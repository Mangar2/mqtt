# connection/test — Unit Test Specification (Module 7)

## ConnectionStateMachine (7.1)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `state_initial_is_connecting` | Default construction | — | state == Connecting |
| `on_connect_transitions_to_connected` | CONNECT received in Connecting state | on_connect() | state == Connected |
| `on_connect_throws_duplicate` | CONNECT received in Connected state | on_connect() twice | ConnectionException(DuplicateConnect) |
| `on_connect_throws_in_disconnecting` | CONNECT in Disconnecting state | on_connect() after on_disconnect() | ConnectionException(InvalidState) |
| `on_connect_throws_in_closed` | CONNECT after close | close() then on_connect() | ConnectionException(InvalidState) |
| `on_disconnect_transitions_to_disconnecting` | DISCONNECT received in Connected | on_connect(), on_disconnect() | state == Disconnecting |
| `on_disconnect_throws_if_not_connected` | DISCONNECT in Connecting state | on_disconnect() | ConnectionException(InvalidState) |
| `on_connection_lost_transitions_to_closed` | TCP loss from Connected | on_connect(), on_connection_lost() | state == Closed |
| `on_connection_lost_from_connecting` | TCP loss before CONNECT | on_connection_lost() | state == Closed |
| `close_transitions_to_closed` | Force close from Connected | on_connect(), close() | state == Closed |
| `is_connected_true_when_connected` | is_connected in Connected | on_connect() | is_connected() == true |
| `is_connected_false_when_not_connected` | is_connected in Connecting | — | is_connected() == false |
| `enforce_not_connecting_passes_in_connected` | Packet allowed after CONNECT | on_connect(), enforce_not_connecting() | no throw |
| `enforce_not_connecting_throws_in_connecting` | Packet before CONNECT | enforce_not_connecting() | ConnectionException(ConnectRequired) |
| `enforce_not_connecting_throws_in_disconnecting` | Packet after DISCONNECT | on_connect(), on_disconnect(), enforce_not_connecting() | ConnectionException(InvalidState) |

## KeepAliveTimer (7.2)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `disabled_when_keep_alive_is_zero` | Keep Alive == 0 | KeepAliveTimer(0) | is_enabled() == false |
| `not_expired_immediately_after_construction` | Timer not expired at t=0 | KeepAliveTimer(60) | is_expired() == false |
| `not_expired_after_reset` | Timer reset before expiry | reset() | is_expired() == false |
| `disabled_timer_never_expires` | Disabled timer | KeepAliveTimer(0) | is_expired() == false always |
| `enabled_when_keep_alive_nonzero` | Keep Alive > 0 | KeepAliveTimer(10) | is_enabled() == true |

## TopicAliasTable (7.3)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `max_aliases_returns_configured_value` | Accessor | TopicAliasTable(10) | max_aliases() == 10 |
| `set_and_get_inbound_alias` | Round-trip inbound mapping | set_inbound(1, "a/b"), get_inbound(1) | "a/b" |
| `set_and_get_outbound_alias` | Round-trip outbound mapping | set_outbound("a/b", 1), get_outbound("a/b") | 1 |
| `get_outbound_unknown_returns_nullopt` | Unknown outbound topic | get_outbound("x") | nullopt |
| `get_inbound_unknown_throws` | Unknown alias lookup | get_inbound(1) | ConnectionException(AliasNotFound) |
| `set_inbound_alias_zero_throws` | Alias 0 is invalid | set_inbound(0, "a") | ConnectionException(AliasOutOfRange) |
| `set_inbound_alias_exceeds_max_throws` | Alias > max | TopicAliasTable(5), set_inbound(6, "a") | ConnectionException(AliasOutOfRange) |
| `set_outbound_alias_zero_throws` | Alias 0 is invalid | set_outbound("a", 0) | ConnectionException(AliasOutOfRange) |
| `set_outbound_alias_exceeds_max_throws` | Alias > max | TopicAliasTable(5), set_outbound("a", 6) | ConnectionException(AliasOutOfRange) |
| `get_inbound_alias_exceeds_max_throws` | Out-of-range lookup | TopicAliasTable(5), get_inbound(6) | ConnectionException(AliasOutOfRange) |
| `reset_clears_inbound_mappings` | reset() removes inbound | set_inbound(1, "a"), reset(), get_inbound(1) | ConnectionException(AliasNotFound) |
| `reset_clears_outbound_mappings` | reset() removes outbound | set_outbound("a", 1), reset(), get_outbound("a") | nullopt |
| `overwrite_inbound_alias` | Reassign same alias | set_inbound(1, "a"), set_inbound(1, "b"), get_inbound(1) | "b" |
| `max_alias_zero_disables_inbound` | max==0 blocks any alias | TopicAliasTable(0), set_inbound(1, "a") | ConnectionException(AliasOutOfRange) |

## ReceiveMaximum (7.4)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `max_returns_configured_value` | Accessor | ReceiveMaximum(10) | max() == 10 |
| `zero_max_defaults_to_65535` | Zero treated as 65535 | ReceiveMaximum(0) | max() == 65535 |
| `acquire_succeeds_within_limit` | Single acquire | ReceiveMaximum(5), acquire() | returns true |
| `acquire_returns_false_at_limit` | Acquire up to max | ReceiveMaximum(2), acquire() x3 | third returns false |
| `is_paused_true_when_limit_reached` | Pause signal | ReceiveMaximum(1), acquire() | is_paused() == true |
| `is_paused_false_initially` | No packets inflight | ReceiveMaximum(10) | is_paused() == false |
| `available_decreases_after_acquire` | Capacity tracking | ReceiveMaximum(5), acquire() x2 | available() == 3 |
| `available_increases_after_release` | Resume after ACK | ReceiveMaximum(2), acquire() x2, release() | available() == 1 |
| `release_restores_capacity` | ACK frees slot | ReceiveMaximum(1), acquire(), release(), acquire() | last acquire returns true |
| `release_throws_when_inflight_zero` | Underflow guard | ReceiveMaximum(5), release() | ConnectionException(InvalidState) |

## ClientHandler placeholder (17)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `client_handler_run_with_connection_pointer` | Run with non-null connection pointer | `unique_ptr<TcpConnection>(k_invalid_socket)` | no throw; returns after close-path |
| `client_handler_run_with_null_connection` | Run with null connection pointer | `nullptr` | no throw; returns immediately |

## ConnectionManager (23)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `connection_manager_start_stop_mqtt` | MQTT listener accepts one client and dispatches callback | mqtt_port=ephemeral, ws_port=0, one loopback connect | callback invoked once; manager stops cleanly |
| `connection_manager_start_stop_ws` | WS listener accepts one client and marks ws path | mqtt_port=0, ws_port=ephemeral, one loopback connect | callback invoked with `is_ws=true`; manager stops cleanly |
| `connection_manager_stop_without_start` | Idempotent shutdown before startup | manager with both ports 0 | no throw; running remains false |
| `connection_manager_start_idempotent` | Double start call on active manager | start() called twice, then one client connect | no throw; manager remains running and handles client |
| `connection_manager_start_failure_resets_running` | Listener bind failure path in start() | occupy port externally, then start manager on same port | start throws; `is_running()==false` |
| `connection_manager_stop_timeout_requests_socket_shutdown` | Timeout branch in stop() with slow callback | short join timeout, callback sleeps longer than timeout | stop returns and manager is stopped |
| `connection_manager_callback_exception_isolated` | Client callback throws | callback throws `runtime_error` | manager survives and stop() succeeds |
