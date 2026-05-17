# Implementation Checklist Plan: RS485Interface

This checklist is release-gating.

Rule:
- Every checkbox must be checked.
- Every checkbox must have objective evidence (test name, log excerpt, or code reference).
- If one checkbox is open, implementation is not accepted.

References:
- spec/yaha/SPEC-rs485interface.md
- spec/yaha/AUDIT-rs485interface-reconstruction-matrix.md

## 1. Preparation Gates

- [ ] Read and freeze the normative baseline from spec/yaha/SPEC-rs485interface.md.
- [ ] Confirm reconstruction baseline modules from spec/@mangar2/rs485interface/.
- [ ] Confirm no functional simplification policy is active for this implementation.
- [ ] Define parity test strategy before coding.

## 2. Configuration and Contract Gates

- [ ] Implement all required config fields and defaults exactly as specified.
- [ ] Enforce address ranges and enum constraints exactly.
- [ ] Preserve trace singular/plural compatibility mismatch behavior.
- [ ] Validate myAddress constraints exactly.
- [ ] Validate explicit topics/address/value constraints exactly.
- [ ] Add deterministic configuration error reporting.

## 3. Serial Protocol Gates

- [ ] Implement v0 decode layout exactly.
- [ ] Implement v1 decode layout exactly.
- [ ] Implement v0 parity verification exactly.
- [ ] Implement v1 CRC16 verification exactly.
- [ ] Implement command-specific decode for h/t/s values exactly.
- [ ] Implement version-rejection behavior for unsupported versions.
- [ ] Implement v1 fixed-length validation exactly.

## 4. Serial Encode Gates

- [ ] Implement getByteArray v0 with exact byte positions.
- [ ] Implement getByteArray v1 with exact byte positions.
- [ ] Implement flags byte composition exactly.
- [ ] Implement CRC byte order low-then-high exactly.
- [ ] Implement parity byte placement exactly.
- [ ] Implement unsupported encode version failure exactly.

## 5. Reader and Framing Gates

- [ ] Implement noise skip rule byte==0 or byte>127 exactly.
- [ ] Implement multi-frame extraction loop exactly.
- [ ] Emit success tuple format exactly.
- [ ] Emit failure tuple format exactly.
- [ ] Preserve failure advance-by-current-message-length quirk.

## 6. RS485State Gates

- [ ] Implement constructor defaults exactly.
- [ ] Implement receiver selection helper exactly.
- [ ] Implement calculateEnableSend helper exactly.
- [ ] Implement updateStateNoMessage timing logic exactly.
- [ ] Implement UNKNOWN transitions exactly.
- [ ] Implement REBOOT transitions exactly.
- [ ] Implement SINGLE transitions exactly.
- [ ] Implement UNREGISTERED transitions exactly.
- [ ] Implement REGISTERED transitions exactly.
- [ ] Implement tokenLost dynamic runtime field behavior.
- [ ] Implement setState side effects including trace logging.

## 7. Token Exchange Gates

- [ ] Process token messages only when command is !.
- [ ] Implement isForMe logic exactly.
- [ ] Implement rightSibling update rule exactly.
- [ ] Implement leftmostSibling update rule exactly.
- [ ] Preserve Math.min null coercion side effect behavior.
- [ ] Emit token signaling frames exactly.
- [ ] Implement ENABLE_SEND receiver selection exactly.
- [ ] Initialize send version from maxVersion.
- [ ] Enable version adaptation only after sending ENABLE_SEND.
- [ ] Apply version downgrade gate exactly.
- [ ] Keep AddressChain behavior non-functional for send decisions.

## 8. Queue and Scheduler Gates

- [ ] Implement queue replacement semantics exactly.
- [ ] Preserve command X non-replacement exception.
- [ ] Implement per-tick order exactly.
- [ ] Implement maySend forced reset in same tick.
- [ ] Implement queue retry increment and threshold exactly.
- [ ] Reset retry counter only on dequeue path in send loop.
- [ ] Implement response dequeue predicate exactly.
- [ ] Preserve reply-flag exclusion from response predicate.
- [ ] Preserve response dequeue without retry-counter reset.
- [ ] Preserve async non-await scheduler behavior compatibility.
- [ ] Overwrite outgoing message version with negotiated scheduler version.

## 9. MQTT Mapping Gates

- [ ] Implement mqtt-to-serial explicit topic mapping exactly.
- [ ] Implement SWITCH_ON and SWITCH_OFF bit additions exactly.
- [ ] Implement address prefix matching case-insensitive exactly.
- [ ] Implement setting suffix matching case-insensitive exactly.
- [ ] Implement interface string-to-value mapping exactly.
- [ ] Implement numeric value bounds checking exactly.
- [ ] Implement serial-to-mqtt reverse mapping exactly.
- [ ] Implement explicit topics reverse switch-bit logic exactly.

## 10. Action Logic Gates

- [ ] Implement /set suffix check with endsWith('/set').
- [ ] Implement /temporary suffix check with endsWith('/temporary').
- [ ] Implement /blink legacy suffix check with endsWith('blink').
- [ ] Implement temporary on-then-off sequence exactly.
- [ ] Implement blink toggling and cycle counts exactly.
- [ ] Implement cached-state usage for blink phase exactly.
- [ ] Implement storeState cache updates exactly.

## 11. Interface Lifecycle Gates

- [ ] Implement serial open retry count and delay exactly.
- [ ] Implement serial send retry behavior and reopen exactly.
- [ ] Implement trace topic runtime update behavior exactly.
- [ ] Implement handleMessage reason annotation behavior exactly.
- [ ] Implement publish callback behavior exactly.
- [ ] Implement getSubscriptions derivation exactly.
- [ ] Implement time-of-day loop message content exactly.
- [ ] Implement time-of-day enqueue cadence exactly.
- [ ] Ensure time-of-day send uses negotiated version in send path.

## 12. Subscriptions and Namespace Gates

- [ ] Implement wildcard start-topic derivation exactly.
- [ ] Implement settings subscription pattern exactly.
- [ ] Implement explicit topics plus wildcard exactly.
- [ ] Preserve legacy control namespace behavior exactly.
- [ ] Implement monitor namespace compatibility mapping if required by runtime boundary.

## 13. Introspection and Logging Gates

- [ ] Implement isInternal command check exactly.
- [ ] Implement isResponseMessage predicate exactly.
- [ ] Implement getLoggingInfo format behavior including token value labels.
- [ ] Implement trace filtering behavior including singular/plural quirk.

## 14. Parity Test Gates

- [ ] Add byte-level encode/decode parity tests for v0 and v1.
- [ ] Add CRC/parity negative tests.
- [ ] Add complete RS485State transition matrix tests.
- [ ] Add addressed and not-addressed dimension tests.
- [ ] Add long deterministic replay tests.
- [ ] Add token-loss timing parity tests.
- [ ] Add queue retry/dequeue interaction tests.
- [ ] Add command X replacement exception tests.
- [ ] Add version negotiation sequence tests.
- [ ] Add trace-level mismatch behavior tests.
- [ ] Add readmessages noise and failure-advance quirk tests.

### 14.1 Mandatory Unit Test Cases (must exist and pass)

SerialMessage codec tests:
- [ ] Test case: serial_message_v0_encode_exact_bytes
- [ ] Test case: serial_message_v1_encode_exact_bytes
- [ ] Test case: serial_message_v0_decode_exact_fields
- [ ] Test case: serial_message_v1_decode_exact_fields
- [ ] Test case: serial_message_decode_h_t_s_fractional_value
- [ ] Test case: serial_message_v0_parity_mismatch_throws
- [ ] Test case: serial_message_v1_crc_mismatch_throws
- [ ] Test case: serial_message_v1_invalid_length_throws
- [ ] Test case: serial_message_unsupported_version_throws
- [ ] Test case: serial_message_response_predicate_ignores_reply_flag

CRC and parity tests:
- [ ] Test case: crc16_known_vector_matches_legacy
- [ ] Test case: parity_xor_known_vector_matches_legacy

ReadMessages tests:
- [ ] Test case: readmessages_skips_zero_and_gt127_noise
- [ ] Test case: readmessages_extracts_multiple_frames_from_single_chunk
- [ ] Test case: readmessages_failure_advances_by_current_message_length_quirk

RS485State tests:
- [ ] Test case: rs485state_constructor_defaults_exact
- [ ] Test case: rs485state_update_no_message_loop_start_short_long_break_and_timeout
- [ ] Test case: rs485state_unknown_transitions_full
- [ ] Test case: rs485state_reboot_transitions_full
- [ ] Test case: rs485state_single_transitions_full
- [ ] Test case: rs485state_unregistered_transitions_full
- [ ] Test case: rs485state_registered_transitions_full
- [ ] Test case: rs485state_token_lost_threshold_exact_40_ticks
- [ ] Test case: rs485state_setstate_trace_logging_side_effect

TokenExchange tests:
- [ ] Test case: token_exchange_processes_only_token_command
- [ ] Test case: token_exchange_is_for_me_logic_exact
- [ ] Test case: token_exchange_right_sibling_update_rule
- [ ] Test case: token_exchange_leftmost_sibling_update_rule
- [ ] Test case: token_exchange_null_coercion_min_side_effect_preserved
- [ ] Test case: token_exchange_emit_enable_send_receiver_selection
- [ ] Test case: token_exchange_first_enable_send_uses_max_version
- [ ] Test case: token_exchange_version_change_only_after_enable_send_gate
- [ ] Test case: token_exchange_version_downgrade_when_received_enable_send_leq_max

SendQueue and Scheduler tests:
- [ ] Test case: sendqueue_replaces_same_sender_receiver_command_except_x
- [ ] Test case: sendqueue_command_x_never_replaced
- [ ] Test case: scheduler_tick_order_state_then_queue_then_maysend_reset
- [ ] Test case: scheduler_retry_counter_increments_until_dequeue_threshold
- [ ] Test case: scheduler_retry_counter_resets_only_on_dequeue_in_send_loop
- [ ] Test case: scheduler_response_match_dequeues_without_retry_counter_reset
- [ ] Test case: scheduler_outgoing_message_version_overwritten_with_negotiated_version

SerialDNS tests:
- [ ] Test case: serialdns_explicit_topic_mqtt_to_serial_mapping
- [ ] Test case: serialdns_switch_on_off_bit_additions_exact
- [ ] Test case: serialdns_address_prefix_matching_case_insensitive
- [ ] Test case: serialdns_setting_suffix_matching_case_insensitive
- [ ] Test case: serialdns_interface_string_to_value_mapping
- [ ] Test case: serialdns_numeric_value_bounds_validation
- [ ] Test case: serialdns_serial_to_mqtt_reverse_mapping_explicit_topic
- [ ] Test case: serialdns_serial_to_mqtt_reverse_mapping_fallback_topic

Actions tests:
- [ ] Test case: actions_set_suffix_requires_slash_set
- [ ] Test case: actions_temporary_suffix_requires_slash_temporary
- [ ] Test case: actions_blink_suffix_uses_endswith_blink_without_leading_slash
- [ ] Test case: actions_temporary_sequence_on_then_off_with_delay
- [ ] Test case: actions_blink_cycle_count_and_toggle_sequence
- [ ] Test case: actions_store_state_updates_topic_cache

RS485Interface tests:
- [ ] Test case: interface_trace_topic_exact_match_updates_trace
- [ ] Test case: interface_handle_message_adds_reason_and_enqueues_reply_true
- [ ] Test case: interface_non_token_serial_message_published_with_configured_qos
- [ ] Test case: interface_serial_open_retries_10_with_15s_delay_policy
- [ ] Test case: interface_serial_send_retries_3_and_reopen
- [ ] Test case: interface_time_of_day_message_content_and_cadence
- [ ] Test case: interface_trace_filter_singular_plural_mismatch_preserved

Subscriptions tests:
- [ ] Test case: derivesubscribes_wildcard_start_topics_generation
- [ ] Test case: derivesubscribes_settings_set_topic_patterns
- [ ] Test case: derivesubscribes_explicit_topics_plus_wildcard
- [ ] Test case: derivesubscribes_legacy_control_namespace_included

### 14.2 Unit Test Acceptance Rule

- [ ] Every mandatory test case listed in 14.1 exists in source control.
- [ ] Every mandatory test case listed in 14.1 passes.
- [ ] No test case in 14.1 is marked skipped, disabled, or TODO.
- [ ] Any failing or missing 14.1 test blocks completion.

## 15. Integration and Deployment Gates

- [ ] Build target wiring complete for RS485 client executable.
- [ ] Runtime config template and parsing integration complete.
- [ ] Packaging integration complete for deployment scripts.
- [ ] Remote deployment integration complete.
- [ ] Service artifacts and ini placement validated.

## 16. Final Acceptance Gates

- [ ] All module gates are checked with evidence.
- [ ] All parity tests pass with zero-delta rule.
- [ ] No open diagnostics in changed files.
- [ ] SPEC and implementation docs are synchronized.
- [ ] Reconstruction matrix remains FULL for all modules.

## Completion Rule

Implementation is considered safely and correctly implemented only when every checkbox in sections 1 through 16 is checked and evidence-backed.
