# TEST_SPEC — session (Module 1.7)

## InflightDirection (1.7.2)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `inflight_direction_values` | Enum values match encoding | `Inbound`, `Outbound` | underlying uint8_t values 0, 1 |

## InflightState (1.7.2)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `inflight_state_values` | All four enum values | `WaitingForPuback`, `WaitingForPubrec`, `WaitingForPubrel`, `WaitingForPubcomp` | underlying uint8_t values 0, 1, 2, 3 |

## InflightEntry (1.7.2)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `inflight_entry_defaults` | Default-constructed | none | `packet_id == 0, qos == AtLeastOnce, state == WaitingForPuback, direction == Outbound` |
| `inflight_entry_set_fields` | All fields populated | `packet_id = 42`, topic, `qos = ExactlyOnce`, `state = WaitingForPubrec`, `direction = Outbound` | all fields read back correctly |
| `inflight_entry_equality` | operator== on identical structs | two default-constructed | equal |
| `inflight_entry_inequality` | operator== on differing structs | differ by `packet_id` | not equal |
| `inflight_entry_qos2_outbound` | QoS 2 outbound initial state | `state = WaitingForPubrec, direction = Outbound` | state and direction correct |
| `inflight_entry_qos2_inbound` | QoS 2 inbound state | `state = WaitingForPubrel, direction = Inbound` | state and direction correct |

## SessionState (1.7.1)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `session_state_defaults` | Default-constructed | none | `client_id empty, subscriptions empty, session_expiry_interval == 0` |
| `session_state_set_fields` | All fields populated | `client_id = "c1"`, one subscription, `expiry = 3600` | all fields read back correctly |
| `session_state_equality` | operator== on identical structs | two default-constructed | equal |
| `session_state_inequality` | operator== on differing structs | differ by `client_id` | not equal |
| `session_state_never_expires` | Maximum expiry value | `session_expiry_interval = 0xFFFF'FFFF` | value preserved exactly |
