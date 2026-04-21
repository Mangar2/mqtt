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
| `find_receive_maximum_returns_value_when_property_present` | Parse CONNECT Receive Maximum property | property list with ReceiveMaximum=7 | returns 7 |
| `find_receive_maximum_returns_nullopt_when_property_missing` | Parse CONNECT properties without Receive Maximum | property list without ReceiveMaximum | returns nullopt |
| `map_codec_error_to_connect_reason_handles_all_key_paths` | CONNECT decode reason mapping coverage | representative codec errors + unknown enum value | UnsupportedProtocolVersion, MalformedPacket, and ProtocolError mappings are correct |
| `map_codec_error_to_runtime_reason_handles_all_key_paths` | Runtime decode reason mapping coverage | representative codec errors + unknown enum value | MalformedPacket and ProtocolError mappings are correct |

## Outbound queue bridge helpers (24)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `drain_pending_outbound_messages_returns_fifo_and_empties_source` | Drain helper preserves FIFO order and empties source queue | source queue with three messages | returned vector contains messages in enqueue order and source becomes empty |
| `transfer_pending_outbound_messages_moves_until_source_empty` | Transfer helper moves all messages when target accepts all | source with N messages, empty target with capacity | moved_count == N and target can pop all N messages in order |
| `transfer_pending_outbound_messages_stops_when_target_rejects` | Transfer helper stops when target push fails | source with three messages, target capacity one | moved_count == 1 and remaining source messages are preserved |

## ClientHandler (24)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `client_handler_run_with_connection_pointer` | Run with non-null connection pointer | `unique_ptr<TcpConnection>(k_invalid_socket)` | no throw; returns after close-path |
| `client_handler_run_with_null_connection` | Run with null connection pointer | `nullptr` | no throw; returns immediately |
| `client_handler_connect_ping_disconnect_roundtrip` | Successful CONNECT handshake plus control packets | CONNECT, PINGREQ, DISCONNECT over one connection | CONNACK then PINGRESP; clean teardown |
| `client_handler_connect_timeout_then_partial_connect_succeeds` | CONNECT wait loop timeout + partial frame reassembly | idle socket timeout, then split CONNECT bytes in two chunks | connection still succeeds and returns CONNACK |
| `client_handler_subscribe_unsubscribe_and_publish` | Broker facade dispatch for common packet types | CONNECT, SUBSCRIBE, PUBLISH(QoS0), UNSUBSCRIBE, DISCONNECT | SUBACK and UNSUBACK responses; QoS0 publish accepted without ACK |
| `client_handler_rejects_non_connect_first_packet` | Protocol enforcement for first packet | PINGREQ as first packet | error CONNACK; handler exits |
| `client_handler_keep_alive_timeout_emits_disconnect` | Keep-alive timeout path | CONNECT(keep_alive=1) with no further traffic | broker sends DISCONNECT(KeepAliveTimeout) |
| `client_handler_connect_malformed_packet_returns_malformed_packet_connack` | Decode error during initial CONNECT handling | malformed CONNECT bytes as first complete packet | CONNACK(MalformedPacket); handler exits |
| `client_handler_runtime_malformed_packet_returns_disconnect` | Decode error in dispatch loop | valid CONNECT then malformed packet | DISCONNECT(MalformedPacket) emitted |
| `client_handler_runtime_invalid_qos_packet_returns_malformed_disconnect` | Runtime malformed branch for invalid QoS flags | valid CONNECT then malformed PUBLISH with invalid QoS bits | DISCONNECT(MalformedPacket) emitted |
| `client_handler_abrupt_socket_close_is_connection_lost_path` | Unclean teardown path | valid CONNECT then remote socket close without DISCONNECT | handler exits without crash via connection-lost path |
| `client_handler_disconnect_with_expiry_override` | DISCONNECT property parsing | valid CONNECT then DISCONNECT with SessionExpiryInterval property | clean disconnect path with parsed override, no crash |
| `client_handler_enhanced_auth_connect_flow` | Multi-step enhanced auth handshake | CONNECT with auth method then AUTH credentials | AUTH challenge then CONNACK success |
| `client_handler_inbound_qos2_publish_rel_flow` | QoS2 inbound state machine path | CONNECT then PUBLISH(QoS2) then PUBREL | emits PUBREC then PUBCOMP |
| `client_handler_second_connect_triggers_protocol_disconnect` | Duplicate CONNECT after established session | CONNECT then second CONNECT | DISCONNECT(ProtocolError) |
| `client_handler_pingresp_packet_hits_default_protocol_error` | Unexpected inbound packet type in dispatch loop | CONNECT then PINGRESP | DISCONNECT(ProtocolError) |
| `client_handler_websocket_upgrade_connect_and_disconnect` | WebSocket transport success path | valid HTTP upgrade, masked CONNECT, masked DISCONNECT | HTTP 101 response and WS-framed CONNACK |
| `client_handler_websocket_invalid_upgrade_closes_connection` | WebSocket upgrade failure path | invalid HTTP upgrade request | handler closes connection without crash |
| `client_handler_connect_auth_failure_returns_error_connack` | CONNECT auth failure path | CONNECT with wrong username/password against configured credential | error CONNACK is returned and connection ends |
| `client_handler_enhanced_auth_wrong_packet_fails_handshake` | Enhanced auth loop failure on non-AUTH packet | CONNECT with auth method, then PINGREQ instead of AUTH | AUTH challenge followed by error CONNACK |
| `client_handler_session_takeover_executes_close_callback` | Session takeover closes old connection with MQTT reason | two CONNECTs with same client ID on two sockets | second handshake succeeds and first connection receives DISCONNECT(0x8E SessionTakenOver) |
| `client_handler_runtime_connack_packet_hits_default_protocol_error` | Unexpected inbound CONNACK packet in dispatch loop | CONNECT then CONNACK from client | DISCONNECT(ProtocolError) |
| `client_handler_runtime_suback_packet_hits_default_protocol_error` | Unexpected inbound SUBACK packet in dispatch loop | CONNECT then SUBACK from client | DISCONNECT(ProtocolError) |
| `client_handler_runtime_unsuback_packet_hits_default_protocol_error` | Unexpected inbound UNSUBACK packet in dispatch loop | CONNECT then UNSUBACK from client | DISCONNECT(ProtocolError) |
| `client_handler_websocket_rejects_non_connect_first_packet` | WebSocket protocol enforcement for first MQTT packet | valid HTTP upgrade, then masked PINGREQ | WS-framed error CONNACK and teardown |
| `client_handler_auth_packet_after_connect_handles_invalid_reauth_without_abort` | Runtime AUTH protocol violation must not abort process | CONNECT via basic auth, then AUTH(ReAuthenticate) without enhanced session | DISCONNECT(ProtocolError) is emitted; handler exits cleanly |
| `client_handler_outbound_qos2_ack_flow_exercises_pubrec_and_pubcomp` | Outbound QoS2 state machine ack handlers | CONNECT, QoS2 subscribe, QoS2 publish loopback, then PUBREC and PUBCOMP for outbound publish | inbound PUBCOMP, outbound PUBLISH, and outbound PUBREL are observed in any order; no crash |
| `client_handler_inbound_qos2_duplicate_publish_releases_inbound_slot` | Duplicate inbound QoS2 should not consume receive window twice | receive_maximum=1, PUBLISH(QoS2,id=61), duplicate PUBLISH(QoS2,id=61), then PUBREL(61) | second PUBLISH accepted and responded with PUBREC; PUBCOMP sent after PUBREL |
| `client_handler_runtime_unknown_pubrel_returns_packet_identifier_not_found` | Unknown inbound PUBREL fallback path | valid CONNECT then PUBREL with unknown packet id | PUBCOMP with `PacketIdentifierNotFound` reason is returned |
| `client_handler_enhanced_auth_malformed_packet_fails_handshake` | Enhanced auth loop decode error path | CONNECT with auth method, then malformed packet bytes instead of AUTH | error CONNACK(MalformedPacket) is returned |
| `client_handler_enhanced_auth_timeout_then_close_aborts_handshake` | Enhanced auth read loop timeout and EOF handling | CONNECT with auth method, no AUTH reply, then peer close | handler exits cleanly without crash |
| `client_handler_runtime_auth_failure_disconnects_cleanly` | Runtime AUTH failure branch | successful enhanced auth session, then AUTH(ReAuthenticate) starts reauth and AUTH(ContinueAuthentication) with invalid credentials fails | AUTH challenge is emitted, then DISCONNECT(BadUserNameOrPassword) and clean loop exit |
| `client_handler_runtime_reauthenticate_failure_disconnects_cleanly` | Runtime AUTH re-authenticate invalid method | successful enhanced auth session, then AUTH(ReAuthenticate) with mismatched Authentication Method | DISCONNECT(ProtocolError) is emitted and loop exits cleanly |
| `client_handler_persistent_reconnect_sets_session_present` | Persistent reconnect resumes existing session | two sequential CONNECTs with same client ID and SessionExpiryInterval > 0 | second CONNACK has `session_present=true` |
| `client_handler_connect_receive_maximum_limits_outbound_inflight_qos1` | Outbound inflight gating uses CONNECT Receive Maximum | CONNECT with ReceiveMaximum=1, self-subscribe QoS1, publish two QoS1 messages without PUBACK for first outbound delivery | second outbound PUBLISH blocked until first outbound PUBACK arrives |
| `client_handler_runtime_qos2_receive_maximum_exceeded_disconnects` | Inbound inflight gating enforces broker receive_maximum | broker config receive_maximum=1, send two QoS2 PUBLISH without PUBREL for first | second QoS2 triggers DISCONNECT(ReceiveMaximumExceeded 0x93) |

## ConnectionManager (23)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `connection_manager_start_stop_mqtt` | MQTT listener accepts one client and dispatches callback | mqtt_port=ephemeral, ws_port=0, one loopback connect | callback invoked once; manager stops cleanly |
| `connection_manager_start_stop_ws` | WS listener accepts one client and marks ws path | mqtt_port=0, ws_port=ephemeral, one loopback connect | callback invoked with `is_ws=true`; manager stops cleanly |
| `connection_manager_stop_without_start` | Idempotent shutdown before startup | manager with both ports 0 | no throw; running remains false |
| `connection_manager_start_idempotent` | Double start call on active manager | start() called twice, then one client connect | no throw; manager remains running and handles client |
| `connection_manager_start_failure_resets_running` | Listener bind failure path in start() | occupy port externally, then start manager on same port | start throws; `is_running()==false` |
| `connection_manager_start_bind_failure_inside_try_resets_running` | Bind conflict after reactor start executes internal catch cleanup | keep one `TcpListener` bound on port P, then `ConnectionManager(P,0,...)` start | start throws from listener creation; `is_running()==false` |
| `connection_manager_start_ws_bind_failure_after_mqtt_listener_cleans_mqtt_listener` | WS bind failure after MQTT listener setup exercises catch cleanup with MQTT listener present | hold a listener on ws_port, start manager with free mqtt_port + occupied ws_port | start throws and manager is not running; cleanup branch with mqtt_listener has_value executed |
| `connection_manager_stop_timeout_requests_socket_shutdown` | Timeout branch in stop() with slow callback | short join timeout, callback sleeps longer than timeout | stop returns and manager is stopped |
| `connection_manager_callback_exception_isolated` | Client callback throws | callback throws `runtime_error` | manager survives and stop() succeeds |
| `connection_manager_io_result_to_string_covers_all_cases` | Internal IoResult string conversion helper mappings | Ok, WouldBlock, Closed, Error, and unknown cast value | returns "ok", "would_block", "closed", "error", and "unknown" |
| `connection_manager_set_socket_blocking_for_test_invalid_handle_returns_false` | Internal blocking-switch helper error branch | invalid socket handle | returns false |
| `connection_manager_close_socket_handle_for_test_invalid_handle_no_throw` | Internal close helper executes close path on invalid handle | invalid socket handle | no throw |
| `connection_manager_handle_accept_ready_invalid_listener_enters_error_path` | Accept loop error branch with invalid listener socket | force running=true and call handle_accept_ready with invalid fd | returns without crash, exercising accept-result error trace path |
