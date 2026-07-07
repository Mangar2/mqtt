# TEST_SPEC — message (Module 1.5)

## Message (1.5.1)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `message_defaults` | Default-constructed | none | `topic == "", payload empty, qos == AtMostOnce, retain == false, properties empty` |
| `message_set_fields` | All fields populated | topic, payload, qos, retain, one property | fields read back correctly |
| `message_equality` | operator== | two identical structs | equal |
| `message_inequality` | operator== | differ by retain | not equal |

## WillMessage (1.5.2)

| Test name | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `will_message_defaults` | Default-constructed | none | `delay_interval == 0, message at defaults` |
| `will_message_with_delay` | Delay set | `delay_interval = 30` | `delay_interval == 30` |
| `will_message_equality` | operator== | two identical structs | equal |
| `will_message_inequality` | operator== | differ by delay_interval | not equal |
