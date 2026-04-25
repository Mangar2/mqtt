# TEST_SPEC.md — broker (Module 15)

All tests are tagged `[broker]`.

---

## broker_config_test.cpp — BrokerConfig + ConfigLoader (15.1)

### ConfigLoader::parse()

| Test case | Section | Scenario | Input | Expected |
|-----------|---------|----------|-------|----------|
| `parse_empty_text_uses_defaults` | defaults | Empty config text | `""` | defaults: mqtt_port=1883, ws_port=0 — but 0,0 would fail validation — so default ws_port leads to NoListenerConfigured |
| `parse_minimal_valid_config` | minimal | Only mqtt_port set | `[network]\nmqtt_port=1883` | mqtt_port=1883, all other fields at defaults |
| `parse_all_network_keys` | network section | Both ports set | `[network]\nmqtt_port=1884\nws_port=9001` | mqtt_port=1884, ws_port=9001 |
| `parse_broker_section` | broker section | All broker keys | max_connections, receive_maximum, etc. | fields match supplied values |
| `parse_persistence_section` | persistence | mode and dir | `mode=no-states`, `dir=/tmp/data` | persistence_mode=NoStates, persistence_dir="/tmp/data" |
| `parse_persistence_mode_invalid_throws` | persistence | invalid mode rejected | `mode=random` | BrokerException(InvalidConfig) |
| `parse_persistence_enabled_backward_compatibility` | persistence | legacy boolean key still mapped | `enabled=true/false` | mapped to persistence_mode Full/Off |
| `parse_auth_credentials_section` | auth section | repeated credential keys | `credential = alice:s3cr3t`, `credential = bob:pwd` | two credential entries parsed in order |
| `parse_acl_rules_section` | acl section | repeated ACL rule keys | `rule = deny,anonymous,publish,a/#` + `rule = allow,dev1,subscribe,b/+` | two ACL rules parsed in order |
| `parse_bool_true_variants` | bool true | "true", "1", "yes" | allow_anonymous=true/1/yes | allow_anonymous=true |
| `parse_bool_false_variants` | bool false | "false", "0", "no" | allow_anonymous=false/0/no | allow_anonymous=false |
| `parse_ignores_comments` | comments | Lines starting with # | `# comment\nmqtt_port=1883` | comment ignored, port parsed |
| `parse_ignores_unknown_keys` | unknown keys | Key not in spec | `[network]\nunknown_key=99\nmqtt_port=1883` | unknown key ignored, port parsed |
| `parse_trims_whitespace` | whitespace | Spaces around = | ` mqtt_port = 1883 ` | port=1883 |
| `parse_bool_invalid_throws` | bad bool | Invalid bool string | `allow_anonymous=maybe` | BrokerException(InvalidConfig) |
| `parse_uint_negative_throws` | bad uint | Negative number text | `max_connections=-1` | BrokerException(InvalidConfig) |
| `parse_uint_overflow_throws` | overflow | Number > UINT32_MAX | digit string exceeding 4294967295 | BrokerException(InvalidConfig) |
| `parse_uint16_overflow_throws` | u16 overflow | Number > 65535 | `mqtt_port=70000` | BrokerException(InvalidConfig) |
| `parse_server_keep_alive_uint16_overflow_throws` | u16 overflow | Number > 65535 | `server_keep_alive=70000` | BrokerException(InvalidConfig) |
| `parse_auth_credential_invalid_format_throws` | bad auth credential | missing `:` separator | `credential=malformed_without_separator` | BrokerException(InvalidConfig) |
| `parse_acl_rule_invalid_format_throws` | bad acl rule | missing one CSV field | `rule=deny,anonymous,publish` | BrokerException(InvalidConfig) |
| `parse_tracing_section_global_level_and_modules` | tracing section | parse global level and module override list | `[tracing]\nglobal_level=info\ntrace_modules=broker,connection` | `trace_global_level=Info`, `trace_modules=["broker","connection"]` |
| `parse_tracing_invalid_level_throws` | tracing section | reject unknown level | `[tracing]\nglobal_level=verbose` | BrokerException(InvalidConfig) |
| `parse_both_ports_zero_throws` | no listener | Both ports absent (0) | `[network]\nmqtt_port=0\nws_port=0` | BrokerException(NoListenerConfigured) |
| `parse_max_connections_zero_throws` | bad max_conn | max_connections=0 | `max_connections=0` | BrokerException(InvalidConfig) |
| `parse_receive_maximum_zero_throws` | bad recv_max | receive_maximum=0 | `receive_maximum=0` | BrokerException(InvalidConfig) |
| `parse_max_queued_zero_throws` | bad queued | max_queued_messages=0 | `max_queued_messages=0` | BrokerException(InvalidConfig) |
| `parse_write_queue_max_bytes_zero_throws` | bad write queue size | write_queue_max_bytes=0 | `write_queue_max_bytes=0` | BrokerException(InvalidConfig) |
| `parse_write_queue_max_bytes_over_hard_limit_throws` | write queue hard cap | value above hard upper bound | `write_queue_max_bytes=<hard_limit+1>` | BrokerException(InvalidConfig) |

### ConfigLoader::load()

| Test case | Section | Scenario | Input | Expected |
|-----------|---------|----------|-------|----------|
| `load_from_file` | file I/O | Valid file on disk | temp file with valid INI content | BrokerConfig with mqtt_port=1884 |
| `load_nonexistent_file_throws` | file error | Non-existent path | "/no/such/file.conf" | BrokerException(InvalidConfig) |

---

## broker_test.cpp — Broker (15.2 + 15.3)

| Test case | Section | Scenario | Input | Expected |
|-----------|---------|----------|-------|----------|
| `broker_initially_not_running` | state | Fresh Broker | default config | is_running() == false |
| `broker_running_after_startup` | startup | startup() on port 0 | mqtt_port=0, ws_port=9 (ephemeral) | is_running() == true |
| `broker_not_running_after_shutdown` | shutdown | startup then shutdown | ephemeral port | is_running() == false after shutdown() |
| `broker_startup_already_running_throws` | double start | Call startup() twice | ephemeral port | BrokerException(AlreadyRunning) |
| `broker_module_accessors_after_startup` | accessors | Access all modules | ephemeral port | no nullptr / no crash |
| `broker_register_unregister_connection` | conn tracking | Register and unregister | client_id="c1" | no crash; after unregister, delivery callback not stored |
| `broker_shutdown_idempotent` | double shutdown | Call shutdown() twice | after startup | no crash / no throw |
| `broker_destructor_auto_shutdown` | destructor | Let Broker go out of scope while running | ephemeral port | no crash |
| `broker_shutdown_requested_false_initially` | signal | Before install_signal_handlers | — | shutdown_requested() == false |
| `broker_with_persistence_startup` | persistence | Startup with full persistence mode | temp dir, persistence_mode=Full | is_running() == true, loads empty stores |
| `broker_persistence_startup_loads_seeded_records` | persistence | Startup with persisted session/retained/inflight snapshots | temp dir with pre-saved records | startup succeeds and clean_start=false reconnect reports session_present=true |
| `broker_persistence_startup_loads_seeded_offline_queue` | persistence | Startup with persisted offline queue snapshot | temp dir with seeded session + offline queue entries | startup succeeds (offline queue restored) |
| `broker_statistics_collector_accessor` | monitoring | statistics_collector() | after startup | snapshot all-zero |
| `broker_register_increments_connected_clients` | monitoring | register/unregister | 2 clients | connected_clients tracks correctly |
| `broker_register_same_client_does_not_double_count` | monitoring | re-register existing client ID | same client registered twice | connected_clients remains 1 |
| `broker_unregister_unknown_client_keeps_count` | monitoring | unregister idempotence | missing + existing client IDs | connected_clients never underflows |
| `broker_handle_publish_counts_inbound_via_facade` | monitoring | handle_publish() facade | 2 publishes | messages_inbound==2 |
| `broker_handle_publish_counts_inbound` | publish facade | handle_publish() wrapper increments inbound stats | 2 publishes | messages_inbound==2 |
| `broker_handle_publish_without_subscribers_is_safe` | monitoring | publish without subscribers | 1 publish | no throw, messages_inbound increments |
| `broker_handle_publish_rejects_zero_topic_alias` | publish facade | reject Topic Alias value 0 before routing | PUBLISH with TopicAlias=0 property | returns `ImplementationSpecificError` |
| `broker_handle_publish_maps_acl_rejection_to_not_authorized` | publish facade | map ACL publish denial | allow_anonymous=false without publish ACL + PUBLISH | returns `NotAuthorized` |
| `broker_handle_publish_maps_invalid_topic_alias_to_protocol_error` | publish facade | map invalid alias usage | inbound PUBLISH with empty topic + alias property | returns `ProtocolError` |
| `broker_handle_publish_maps_online_queue_full_to_quota_exceeded` | publish facade | map outbound queue capacity failure for QoS1/2 | subscribed online client queue already full + subscription max QoS1 + QoS1 publish | returns `QuotaExceeded` |
| `broker_handle_publish_maps_frame_too_large_to_quota_exceeded` | publish facade | map write-queue byte-cap overflow | small `write_queue_max_bytes` + large routed publish | returns `QuotaExceeded` |
| `broker_handle_publish_with_null_registered_queue_is_safe` | publish facade | tolerate null registered queue | register client with null queue and publish routed message | returns `Success` without crash |
| `broker_handle_subscribe_returns_suback_and_delivers_retained` | subscribe facade | subscribe to filter with retained message present | retained on matching topic + SUBSCRIBE QoS1 | SUBACK GrantedQoS1 and retained message delivered |
| `broker_handle_subscribe_denied_returns_not_authorized` | subscribe facade | ACL denies subscription | allow_anonymous=false + SUBSCRIBE | SUBACK reason NotAuthorized |
| `broker_handle_unsubscribe_removes_subscription` | unsubscribe facade | subscribe then unsubscribe | publish before and after unsubscribe | first publish delivered, second suppressed; UNSUBACK Success |
| `broker_tick_returns_false_when_sys_disabled` | monitoring | tick() with interval=0 | far-future now | returns false |
| `broker_tick_publishes_sys_topics_when_enabled` | monitoring | tick() with interval=60 | far-future now | returns true |
| `broker_tick_handles_session_expiry_and_will_publish` | housekeeping | tick() runs will/session housekeeping | CONNECT with will and expiry, then connection_lost and tick at expiry | session removed and delayed will emitted through router |
| `broker_tick_with_no_housekeeping_work_is_safe` | housekeeping | tick() when no due will and no expired session | normal startup + immediate tick | no throw, no side effects |
| `broker_handle_connect_returns_connect_result` | connect facade | wrapped connect workflow | clean_start=false CONNECT | returns ConnectResult with Success and session_present=false |
| `broker_handle_connect_auth_failure_returns_reason` | connect facade | auth denied in password mode | allow_anonymous=false + CONNECT without creds | ConnectResult.reason_code == BadUserNameOrPassword |
| `broker_handle_connect_password_auth_success_with_configured_credential` | connect facade | auth allowed in password mode | allow_anonymous=false + configured credential + matching CONNECT username/password | ConnectResult.reason_code == Success |
| `broker_handle_connect_enhanced_sets_auth_method` | connect facade | enhanced method present in CONNECT | AuthenticationMethod=PLAIN | auth_status=Success, auth_method="PLAIN" |
| `broker_handle_connect_enhanced_continue_in_password_mode` | connect facade | enhanced auth challenge flow starts | password mode + method=PLAIN + missing inline creds | auth_status=Continue, reason=ContinueAuthentication |
| `broker_handle_connect_enhanced_bad_method_fails` | connect facade | enhanced CONNECT method unsupported | password mode + method=SCRAM | auth_status=Failure, reason=BadAuthenticationMethod |
| `broker_handle_auth_packet_completes_pending_exchange_success` | connect facade | pending enhanced exchange completed by AUTH | prior CONNECT Continue + AUTH method=PLAIN + auth_data user:pass | auth_status=Success, reason=Success |
| `broker_handle_auth_packet_failure_ends_pending_exchange` | connect facade | pending exchange fails and context cleared | prior CONNECT Continue + AUTH bad method | first call BadAuthenticationMethod, next call ProtocolError |
| `broker_handle_auth_packet_missing_data_returns_continue` | connect facade | pending exchange requests more AUTH data | prior CONNECT Continue + AUTH method=PLAIN without auth_data | auth_status=Continue, reason=ContinueAuthentication |
| `broker_handle_auth_packet_malformed_data_returns_failure` | connect facade | pending exchange receives malformed AUTH payload | prior CONNECT Continue + AUTH method=PLAIN + malformed auth_data | auth_status=Failure, reason=BadUserNameOrPassword |
| `broker_handle_auth_packet_without_pending_exchange_protocol_error` | connect facade | AUTH arrives without pending enhanced exchange | client without pending context | auth_status=Failure, reason_code=ProtocolError |
| `broker_handle_reauthenticate_success_for_enhanced_session` | connect facade | re-auth with matching method on enhanced session | CONNECT method PLAIN, AUTH(ReAuthenticate, method=PLAIN) | AuthResult Success |
| `broker_handle_reauthenticate_bad_method_returns_reason` | connect facade | re-auth with wrong method | CONNECT method PLAIN, AUTH(ReAuthenticate, method=SCRAM) | AuthResult Failure/BadAuthenticationMethod |
| `broker_handle_reauthenticate_without_enhanced_session_protocol_error` | connect facade | re-auth request without active enhanced context | missing client + AUTH(ReAuthenticate) | AuthResult Failure/ProtocolError |
| `broker_handle_reauthenticate_bad_credentials_returns_failure` | connect facade | re-auth payload has wrong credentials | CONNECT + initial auth_data valid, then re-auth auth_data invalid | AuthResult Failure/BadUserNameOrPassword |
| `broker_handle_connect_builds_connack_properties` | connect facade | connack properties from config | receive_maximum/topic_alias_maximum set | ConnectResult contains matching ReceiveMaximum and TopicAliasMaximum properties |
| `broker_handle_connect_omits_server_keep_alive_when_disabled` | connect facade | server_keep_alive default disabled | server_keep_alive=0 | ConnectResult omits ServerKeepAlive property |
| `broker_handle_connect_emits_info_trace` | tracing | CONNECT handling emits info trace in JSON lines | global trace level=info + successful CONNECT | sink receives one JSON line with module `broker` and info `connect_handled` |
| `broker_runtime_trace_system_message_updates_global_level` | tracing | runtime system message overrides global level | topic `$SYS/broker/tracing/global` + payload `trace` | `StructuredTracer::global_level()==Trace` |
| `broker_runtime_trace_system_message_updates_module_override` | tracing | runtime system message enables/disables module trace override | topic `$SYS/broker/tracing/module/connection` + payload `trace` then `none` | `should_emit(trace,"connection")` toggles true then false (under global error) |
| `broker_runtime_trace_system_message_trims_payload_values` | tracing | runtime payload values are trimmed before parsing | payloads with leading/trailing whitespace (`"  info  "`, `"  on  "`, `"  off  "`) | global level and module override are applied correctly |
| `broker_runtime_trace_system_message_ignores_invalid_inputs` | tracing | invalid tracing system messages are ignored | unknown topic, empty module suffix, invalid payload text | tracer state remains unchanged |
| `broker_handle_connect_invalid_client_id_returns_reason` | connect facade | session manager rejects empty client id | CONNECT with empty client_id | ConnectResult.reason_code == ClientIdentifierNotValid |
| `broker_handle_connect_with_will_properties_succeeds` | connect facade | connect contains will delay and will properties | CONNECT with will + WillDelayInterval + ContentType | returns Success without throw |
| `broker_handle_disconnect_unregisters_client` | concurrency facade | wrapped disconnect path | registered client + ReasonCode::Success | connected_clients decremented to 0 |
| `broker_handle_connection_lost_unregisters_client` | concurrency facade | wrapped connection-loss path | registered client + stored will | connected_clients decremented to 0 |
| `broker_reactor_accept_invokes_client_handler` | reactor accept | real TCP loopback client connects | mqtt_port=18885 | reactor accept callback runs client handler path and shutdown succeeds |

---

## enhanced_auth_registry_test.cpp — EnhancedAuthRegistry (threading step 03)

| Test case | Section | Scenario | Input | Expected |
|-----------|---------|----------|-------|----------|
| `enhanced_auth_registry_upsert_pending_then_erase_pending` | pending map | pending context lifecycle | insert pending for client, erase pending | pending entry removed, active entry untouched |
| `enhanced_auth_registry_upsert_active_then_erase_active` | active map | active context lifecycle | insert active for client, erase active | active entry removed, pending entry untouched |
| `enhanced_auth_registry_erase_client_clears_pending_and_active` | clear both | full client cleanup | insert pending + active then erase_client | both maps no longer contain client |
