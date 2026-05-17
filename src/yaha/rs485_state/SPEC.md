# rs485_state

Phase 3/5 scope in this module:
- RS485 token state machine (`RS485State`) with legacy transition and timer behavior
- token exchange logic (`RS485TokenExchange`) with sibling/address-chain handling and version negotiation
- send queue replacement semantics (`RS485SendQueue`)
- scheduler tick behavior (`RS485Scheduler`) including retry/dequeue rules
- mandatory parity verification harness for `RS485State` legacy behavior

## Public API

### Enum Rs485StateId

States:
- Unknown (0)
- Reboot (1)
- Single (2)
- Unregistered (3)
- Registered (4)

### Enum Rs485StateResult

Outputs:
- Unchanged (0)
- EnableSend (1)
- RegistrationInfo (2)
- RegistrationRequest (3)
- StateChanged (4)

### Class Rs485State

Responsibilities:
- process token events and loop pseudo-events
- maintain siblings, timer, may-send flag, and token-lost runtime flag
- expose deterministic state updates (`updateState`, `updateStateNoMessage`)

Legacy compatibility details:
- constructor defaults and dynamic `tokenLost` field are preserved
- `setState` resets timer and optionally emits one trace log line
- receiver-selection and enable-send calculation follow legacy order exactly

Phase-5 parity harness artifacts:
- `test/generate_rs485_state_parity_fixture.js` executes legacy JS `rs485state.js` as oracle.
- `test/rs485_state_parity_fixture.txt` is generated golden snapshot data.
- `test/rs485_state_parity_test.cpp` replays fixtures and enforces zero-delta equality per step.

### Class Rs485TokenExchange

Responsibilities:
- accept token serial frames (`command == '!'`) and update state
- maintain right/left sibling tracking and sorted address chain
- create token signaling frames from state outputs
- adapt negotiated send version only after first outgoing EnableSend

Legacy compatibility detail:
- leftmost-sibling `Math.min` null-coercion behavior is preserved

### Class Rs485SendQueue

Responsibilities:
- FIFO queue for outbound serial frames
- replacement by `(sender, receiver, command)` equality
- command `X` never replaces existing queued messages

### Class Rs485Scheduler

Responsibilities:
- process received serial frames and pass token frames to token exchange
- run one tick (`processTick`) with fixed ordering:
  1. state update without message and optional token send
  2. queue send only when `maySend` is true
  3. force-reset `maySend` in same tick
- track retry counter for queued sends

Legacy compatibility details:
- queued message dequeue on response does not reset retry counter
- retry counter resets only when dequeue happens in queue-send path
- outbound frame version is overwritten by negotiated token-exchange version
