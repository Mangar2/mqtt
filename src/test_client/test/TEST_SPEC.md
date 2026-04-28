# test_client/test — Unit tests for Step 27-32 shell

## Profile Model

| Test case | Tag | Description |
|-----------|-----|-------------|
| `test_client_profile_defaults_validate` | `[test_client][profile]` | Default profile values are valid for Step 27 shell constraints. |
| `test_client_profile_save_and_load_roundtrip_preserves_values` | `[test_client][profile]` | Profile persistence roundtrip preserves configured host/transport/ws headers/auth/reconnect values. |
| `test_client_profile_load_rejects_unknown_key` | `[test_client][profile]` | Loading profile file with unsupported key fails with parse error. |
| `test_client_profile_validate_rejects_invalid_ws_path` | `[test_client][profile]` | WS transport profile requires `ws_path` starting with `/`. |
| `test_client_profile_validation_and_override_error_paths` | `[test_client][profile]` | Invalid host/port/client-id/timeout/auth values and malformed overrides are rejected. |
| `test_client_profile_transport_helpers_cover_all_variants` | `[test_client][profile]` | Transport string conversion/parsing covers mqtt/ws and invalid input branch. |
| `test_client_profile_step28_step29_roundtrip_preserves_extended_fields` | `[test_client][profile]` | Extended CONNECT and PUBLISH profile fields for Step 28/29 are persisted and loaded without loss. |
| `test_client_profile_step28_step29_validation_rejects_invalid_combinations` | `[test_client][profile]` | Validation rejects invalid QoS ranges, missing will topic for will properties, unsupported encodings, and ambiguous publish payload sources. |
| `test_client_profile_step30_subscribe_fields_roundtrip_and_validation` | `[test_client][profile]` | Subscribe profile fields (subscription entries, output pipeline controls, identifiers, user properties) are persisted and validation rejects invalid combinations/encodings. |

## CLI Parsing

| Test case | Tag | Description |
|-----------|-----|-------------|
| `test_client_cli_connect_parses_profile_and_overrides` | `[test_client][cli]` | `connect` command parses profile file option and field overrides correctly. |
| `test_client_cli_save_profile_requires_output` | `[test_client][cli]` | `save-profile` command without `--output` fails fast with clear argument error. |
| `test_client_cli_show_profile_parses_basic_options` | `[test_client][cli]` | `show-profile` command parses profile path and optional overrides. |
| `test_client_cli_rejects_unknown_option` | `[test_client][cli]` | Unknown CLI flags are rejected as argument errors. |
| `test_client_cli_save_profile_with_all_supported_options_succeeds` | `[test_client][cli]` | `save-profile` parses `--output` and all supported override flags successfully. |
| `test_client_cli_help_and_error_paths` | `[test_client][cli]` | Help mode, unknown command, and missing option values follow expected parser behavior. |
| `test_client_cli_publish_parses_step29_options` | `[test_client][cli]` | `publish` command parses topic/QoS/payload modes and MQTT 5 PUBLISH property flags. |
| `test_client_cli_connect_parses_step28_options` | `[test_client][cli]` | `connect` command parses MQTT 5 CONNECT property and will-option flags. |
| `test_client_cli_subscribe_parses_step30_options` | `[test_client][cli]` | `subscribe` command parses subscription entries, MQTT 5 subscribe properties, and output pipeline flags. |
| `test_client_cli_scenario_parses_step31_options` | `[test_client][cli]` | `scenario` command parses selected built-in scenario name and common overrides. |
| `test_client_cli_scenario_parses_step32_load_options` | `[test_client][cli]` | `scenario` command parses Step 32 load-mode selector and tuning flags. |
| `test_client_cli_scenario_requires_selector` | `[test_client][cli]` | `scenario` command rejects calls without `--scenario`, `--load-mode`, or `--list-scenarios`. |
| `test_client_cli_publish_command_alias_pub_is_supported` | `[test_client][cli]` | `pub` command alias maps to publish flow and parses core short publish flags. |
| `test_client_cli_mqttx_publish_input_aliases_are_supported` | `[test_client][cli]` | mqttx-style publish payload/property aliases (`-s`, `-M`, `--file-read`, `-f`, `-pf`, `-e`, `-ta`, `-rt`, `-cd`, `-up`, `-si`, `-ct`) map to profile overrides. |
| `test_client_cli_mqttx_connection_aliases_are_supported` | `[test_client][cli]` | mqttx-style host/port/client/auth/connect-property aliases map to supported test-client options. |
| `test_client_cli_mqttx_will_aliases_are_supported` | `[test_client][cli]` | mqttx-style will aliases (`-Wt`..`-Wup`) map to supported will profile overrides. |
| `test_client_cli_mqttx_version_alias_rejects_non_v5` | `[test_client][cli]` | mqttx version alias accepts only MQTT 5.0 and rejects unsupported versions. |
| `test_client_cli_wp1_version_flags_are_supported` | `[test_client][cli]` | Top-level version flags (`--version`, `-v`) are parsed as dedicated version command mode. |
| `test_client_cli_wp1_stub_commands_help_flow_is_supported` | `[test_client][cli]` | mqttx compatibility stub commands (`conn`, `sub`, `simulate`, `ls`, `init`, `check`) accept `--help` and resolve to help mode. |
| `test_client_cli_wp1_stub_commands_without_help_fail` | `[test_client][cli]` | mqttx compatibility stub commands reject non-help execution until runtime behavior is implemented. |
| `test_client_cli_wp1_bench_help_flows_are_supported` | `[test_client][cli]` | Bench entry and bench subcommands accept `--help` and return help mode instead of argument errors. |
| `test_client_cli_wp2_reconnect_alias_maximun_is_supported` | `[test_client][cli]` | mqttx simulate alias spelling `--maximun-reconnect-times` is accepted in mqttx-compatible pub/bench flows and mapped to reconnect override. |
| `test_client_cli_wp2_pub_rejects_not_implemented_debug_save_load_options` | `[test_client][cli]` | mqttx pub compatibility path rejects recognized but not implemented `--debug`, `--save-options`, and `--load-options` with argument errors. |
| `test_client_cli_wp2_bench_rejects_not_implemented_debug_save_load_options` | `[test_client][cli]` | mqttx bench compatibility path rejects recognized but not implemented `--debug`, `--save-options`, and `--load-options` with argument errors. |
| `test_client_cli_wp3_bench_verbose_is_not_metrics_json` | `[test_client][cli]` | Bench `-v/--verbose` enables verbose bench output semantics without implicitly enabling metrics-json output. |
| `test_client_cli_wp3_bench_split_and_payload_size_are_parsed` | `[test_client][cli]` | Bench publish parser stores `--split` delimiter and `-S/--payload-size` for runtime payload generation behavior. |
| `test_client_cli_wp3_bench_limit_zero_is_parsed` | `[test_client][cli]` | Bench publish parser preserves `-L/--limit 0` as unlimited-mode signal instead of remapping to bounded operation count. |
| `test_client_cli_wp4_pub_payload_schema_and_size_options_are_parsed` | `[test_client][cli]` | mqttx pub parser maps `-Pp`, `-Pmn`, and `-S` to effective publish runtime overrides. |
| `test_client_cli_wp4_bench_pub_publish_properties_and_schema_flags_are_parsed` | `[test_client][cli]` | mqttx bench pub parser maps publish-property flags and schema/encoding options into profile overrides for runtime application. |

## Scenario Runner

| Test case | Tag | Description |
|-----------|-----|-------------|
| `test_client_scenario_catalog_lists_step31_builtins` | `[test_client][scenario]` | Built-in scenario catalog lists the expected Step 31 scenario names. |
| `test_client_scenario_command_list_mode_succeeds` | `[test_client][scenario]` | Scenario command prints catalog in list mode and exits successfully. |
| `test_client_scenario_command_unknown_name_fails_fast` | `[test_client][scenario]` | Unknown scenario names fail with explicit argument error. |
| `test_client_scenario_command_executes_qos1_scenario_successfully` | `[test_client][scenario]` | QoS1 built-in scenario executes full step chain and returns success with a mock executable. |
| `test_client_scenario_command_propagates_step_failures` | `[test_client][scenario]` | Step execution failure in built-in scenarios returns non-zero exit code. |
| `test_client_scenario_command_runs_step32_mass_connect_mode` | `[test_client][scenario]` | Step 32 `mass-connect` mode executes generated load operations successfully with a mock executable. |
| `test_client_scenario_command_runs_step32_publish_rate_mode` | `[test_client][scenario]` | Step 32 `publish-rate` mode executes burst publish loop successfully with a mock executable. |
| `test_client_scenario_command_runs_step32_multi_subscribe_mode` | `[test_client][scenario]` | Step 32 `multi-subscribe` mode executes concurrent subscriber/publisher workflow successfully with a mock executable. |
| `test_client_scenario_command_rejects_unknown_step32_mode` | `[test_client][scenario]` | Step 32 runner rejects unsupported load-mode names with non-zero result. |
