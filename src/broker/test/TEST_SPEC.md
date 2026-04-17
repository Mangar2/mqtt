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
| `parse_persistence_section` | persistence | enabled and dir | `enabled=true`, `dir=/tmp/data` | persistence_enabled=true, persistence_dir="/tmp/data" |
| `parse_bool_true_variants` | bool true | "true", "1", "yes" | allow_anonymous=true/1/yes | allow_anonymous=true |
| `parse_bool_false_variants` | bool false | "false", "0", "no" | allow_anonymous=false/0/no | allow_anonymous=false |
| `parse_ignores_comments` | comments | Lines starting with # | `# comment\nmqtt_port=1883` | comment ignored, port parsed |
| `parse_ignores_unknown_keys` | unknown keys | Key not in spec | `[network]\nunknown_key=99\nmqtt_port=1883` | unknown key ignored, port parsed |
| `parse_trims_whitespace` | whitespace | Spaces around = | ` mqtt_port = 1883 ` | port=1883 |
| `parse_bool_invalid_throws` | bad bool | Invalid bool string | `allow_anonymous=maybe` | BrokerException(InvalidConfig) |
| `parse_uint_negative_throws` | bad uint | Negative number text | `max_connections=-1` | BrokerException(InvalidConfig) |
| `parse_uint_overflow_throws` | overflow | Number > UINT32_MAX | digit string exceeding 4294967295 | BrokerException(InvalidConfig) |
| `parse_uint16_overflow_throws` | u16 overflow | Number > 65535 | `mqtt_port=70000` | BrokerException(InvalidConfig) |
| `parse_both_ports_zero_throws` | no listener | Both ports absent (0) | `[network]\nmqtt_port=0\nws_port=0` | BrokerException(NoListenerConfigured) |
| `parse_max_connections_zero_throws` | bad max_conn | max_connections=0 | `max_connections=0` | BrokerException(InvalidConfig) |
| `parse_receive_maximum_zero_throws` | bad recv_max | receive_maximum=0 | `receive_maximum=0` | BrokerException(InvalidConfig) |
| `parse_max_queued_zero_throws` | bad queued | max_queued_messages=0 | `max_queued_messages=0` | BrokerException(InvalidConfig) |

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
| `broker_with_persistence_startup` | persistence | Startup with persistence enabled | temp dir, persistence_enabled=true | is_running() == true, loads empty stores |
