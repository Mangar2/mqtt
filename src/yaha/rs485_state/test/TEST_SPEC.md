# rs485_state test specification

## Scope

Unit tests for RS485 phase-3 state machine, token exchange, send queue, and scheduler behavior.

Phase-5 extension in this directory:
- legacy-JS oracle parity fixture generation for `RS485State`
- transition-matrix parity replay against generated golden fixture
- long deterministic replay parity against generated golden fixture

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `rs485_state_constructor_defaults_exact` | constructor default state contract | new `Rs485State` | state unknown, timer 0, siblings empty, maySend false, trace false |
| `rs485_state_unknown_registration_request_transitions_to_unregistered` | unknown-state registration request rule | request `RegistrationRequest` in unknown state | returns `RegistrationInfo`, state becomes unregistered |
| `rs485_state_registered_token_lost_threshold_exact_40_ticks` | token-lost timing behavior in registered short-break processing | registered state plus 44 no-message ticks | short-break result after threshold emits `EnableSend` and `tokenLost` is true |
| `rs485_state_setstate_trace_logging_side_effect` | trace logging side effect on state change | trace enabled and loop-timeout transition | one trace line emitted with state text |
| `rs485_token_exchange_processes_only_token_command` | token filter rule | non-token serial message | no state signaling message generated |
| `rs485_token_exchange_null_coercion_min_side_effect_preserved` | leftmost sibling null-coercion quirk | token from address greater than myAddress while leftmost is null | leftmost sibling becomes `0` |
| `rs485_token_exchange_version_change_only_after_enable_send_gate` | version negotiation gate | incoming enable-send with lower version before and after outgoing enable-send | no version change before gate, version downgraded after gate |
| `rs485_send_queue_replaces_same_sender_receiver_command_except_x` | queue replacement semantics | enqueue same sender/receiver/command and command != `X` | second message replaces first |
| `rs485_send_queue_command_x_never_replaced` | command X exception | enqueue same sender/receiver with command `X` twice | queue contains both messages |
| `rs485_scheduler_tick_order_state_then_queue_then_maysend_reset` | tick ordering and maySend reset | maySend true with one queued message | one queue send and maySend reset false in same tick |
| `rs485_scheduler_response_match_dequeues_without_retry_counter_reset` | response dequeue quirk | queued reply=true message sent once, then matching response received | queue dequeued and retry counter unchanged |
| `rs485_scheduler_retry_counter_resets_only_on_dequeue_in_send_loop` | retry reset path | reply=true message sent across 10 maySend ticks | queue dequeued and retry counter reset to 0 |
| `rs485_state_transition_matrix_matches_legacy_oracle_fixture` | matrix parity check for all reachable states and request/notForMe combinations | generated oracle fixture cases prefixed `matrix_` | result, state, timer, maySend, siblings, and tokenLost exactly match on every step |
| `rs485_state_long_replay_matches_legacy_oracle_fixture` | long mixed replay parity check | generated oracle fixture case `long_replay` | zero-delta step-by-step match against legacy oracle snapshot sequence |
