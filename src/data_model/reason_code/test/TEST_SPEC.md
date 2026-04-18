# TEST_SPEC — reason_code (Module 1.2)

## ReasonCode enum values (1.2.1)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `rc_success_value` | Wire value of Success | none | `0x00` |
| `rc_granted_qos1_value` | Wire value | none | `0x01` |
| `rc_granted_qos2_value` | Wire value | none | `0x02` |
| `rc_disconnect_with_will_value` | Wire value | none | `0x04` |
| `rc_unspecified_error_value` | Wire value | none | `0x80` |
| `rc_wildcard_not_supported_value` | Wire value of last code | none | `0xA2` |
| `rc_aliases` | k_normal_disconnection and k_granted_qos0 equal Success | none | both `== ReasonCode::Success` |

## Classification (1.2.2)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `rc_is_success_true` | Success codes | `Success`, `GrantedQoS1`, `ContinueAuthentication` | `is_success == true` |
| `rc_is_success_false` | Error code | `UnspecifiedError` | `is_success == false` |
| `rc_is_error_true` | Error codes | `UnspecifiedError`, `NotAuthorized`, `WildcardSubscriptionsNotSupported` | `is_error == true` |
| `rc_is_error_false` | Success code | `Success` | `is_error == false` |
| `rc_boundary_0x7F` | Hypothetical boundary | raw cast to `0x7F` | `is_success == true` |
| `rc_boundary_0x80` | First error code | raw cast to `0x80` | `is_error == true` |
| `rc_qos_to_granted_reason` | QoS mapping helper | QoS 0/1/2 | Success / GrantedQoS1 / GrantedQoS2 |
| `rc_qos_to_granted_reason_invalid` | QoS mapping fallback | invalid enum value cast | `UnspecifiedError` |
