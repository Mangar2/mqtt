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
| `test_client_cli_wp1_stub_commands_help_flow_is_supported` | `[test_client][cli]` | mqttx compatibility command-family entries (`conn`, `simulate`, `ls`, `init`, `check`) accept `--help` and resolve to help mode. |
| `test_client_cli_wp1_stub_commands_without_help_fail` | `[test_client][cli]` | Remaining compatibility stub command `conn` rejects non-help execution; simulate still rejects invocations without mandatory selector arguments. |
| `test_client_cli_wp1_bench_help_flows_are_supported` | `[test_client][cli]` | Bench entry and bench subcommands accept `--help` and return help mode instead of argument errors. |
| `test_client_cli_wp2_reconnect_alias_maximun_is_supported` | `[test_client][cli]` | mqttx simulate alias spelling `--maximun-reconnect-times` is accepted in mqttx-compatible pub/bench flows and mapped to reconnect override. |
| `test_client_cli_wp2_pub_rejects_not_implemented_debug_save_load_options` | `[test_client][cli]` | mqttx pub compatibility path rejects recognized but not implemented `--debug`, `--save-options`, and `--load-options` with argument errors. |
| `test_client_cli_wp2_bench_rejects_not_implemented_debug_save_load_options` | `[test_client][cli]` | mqttx bench compatibility path rejects recognized but not implemented `--debug`, `--save-options`, and `--load-options` with argument errors. |
| `test_client_cli_wp3_bench_verbose_is_not_metrics_json` | `[test_client][cli]` | Bench `-v/--verbose` enables verbose bench output semantics without implicitly enabling metrics-json output. |
| `test_client_cli_wp3_bench_split_and_payload_size_are_parsed` | `[test_client][cli]` | Bench publish parser stores `--split` delimiter and `-S/--payload-size` for runtime payload generation behavior. |
| `test_client_cli_wp3_bench_limit_zero_is_parsed` | `[test_client][cli]` | Bench publish parser preserves `-L/--limit 0` as unlimited-mode signal instead of remapping to bounded operation count. |
| `test_client_cli_wp4_pub_payload_schema_and_size_options_are_parsed` | `[test_client][cli]` | mqttx pub parser maps `-Pp`, `-Pmn`, and `-S` to effective publish runtime overrides. |
| `test_client_cli_wp4_bench_pub_publish_properties_and_schema_flags_are_parsed` | `[test_client][cli]` | mqttx bench pub parser maps publish-property flags and schema/encoding options into profile overrides for runtime application. |
| `test_client_cli_wp5_sub_command_maps_mqttx_aliases` | `[test_client][cli]` | mqttx `sub` parser maps topic/subscribe/output aliases into effective subscribe profile overrides. |
| `test_client_cli_wp5_bench_sub_option_semantics_are_parsed` | `[test_client][cli]` | mqttx `bench sub` parser stores runtime semantics for qos/no-local/retain-as-published/retain-handling/subscription-identifier. |
| `test_client_cli_wp6_simulate_maps_to_step32_load_mode` | `[test_client][cli]` | mqttx `simulate -sc <mass-connect|publish-rate|multi-subscribe>` maps to Step32 load-mode execution options and template normalization. |
| `test_client_cli_wp6_ls_scenarios_maps_to_scenario_list_mode` | `[test_client][cli]` | mqttx `ls --scenarios|-sc` maps to scenario catalog list mode. |
| `test_client_cli_wp6_init_and_check_commands_are_parsed` | `[test_client][cli]` | `init` and `check` command families parse as dedicated runtime command modes. |
| `test_client_cli_mqttx_pub_accepts_avsc_alias_in_pub_mode` | `[test_client][cli]` | mqttx `pub` parser maps `-Ap|--avsc-path` to `publish_avsc_path` override in direct pub mode. |
| `test_client_cli_mqttx_pub_rejects_secure_tls_flags` | `[test_client][cli]` | mqttx `pub` parser rejects secure TLS options (`--key`, `--cert`, `--ca`, `--insecure`, `--alpn`) with explicit unsupported error. |
| `test_client_cli_publish_command_with_short_aliases_covers_common_parser_paths` | `[test_client][cli]` | `publish` command with short mqttx-style aliases covers common parser override mapping paths for publish/connection/will/auth options. |
| `test_client_cli_publish_command_with_long_options_covers_common_parser_paths` | `[test_client][cli]` | `publish` command with long-form options covers common parser mapping for connect/publish/subscribe-compatible override keys. |
| `test_client_cli_publish_command_covers_remaining_common_error_and_alias_paths` | `[test_client][cli]` | `publish` command covers remaining common parser branches (`-V` invalid, `--maximun-reconnect-times`, `--save-options`, `--load-options`, `--debug`, and long payload-format flag). |
| `test_client_cli_pub_help_shortcuts_return_help` | `[test_client][cli]` | mqttx `pub --help|-h` is mapped to help command mode. |
| `test_client_cli_ls_and_bench_error_paths` | `[test_client][cli]` | parser rejects unsupported `ls` payload, missing `bench` subcommand, and unknown `bench` subcommand. |
| `test_client_cli_mqttx_pub_long_alias_variants_are_supported` | `[test_client][cli]` | mqttx `pub` parser accepts long alias variants for message/payload/property/connection/will options and maps them to overrides. |
| `test_client_cli_mqttx_pub_long_payload_format_indicator_is_supported` | `[test_client][cli]` | mqttx `pub` parser accepts long `--payload-format-indicator` alias and maps it to `publish_payload_format_indicator`. |
| `test_client_cli_mqttx_sub_long_alias_variants_are_supported` | `[test_client][cli]` | mqttx `sub` parser accepts long alias variants for topic/qos/no-local/retain/output/schema flags and maps them to overrides. |
| `test_client_cli_scenario_list_scenarios_flag_is_parsed` | `[test_client][cli]` | `scenario --list-scenarios` toggles list mode through common options parser path. |
| `test_client_cli_publish_payload_format_indicator_long_flag_is_parsed` | `[test_client][cli]` | `publish --payload-format-indicator` maps to publish payload-format override through common options parser path. |
| `test_client_cli_branch_sweep_executes_many_option_paths` | `[test_client][cli]` | broad parser sweep executes many command/option branches (success and error paths) to guard CLI compatibility behavior. |
| `test_client_cli_mqttx_sub_compact_qos_and_default_output_mode_are_supported` | `[test_client][cli]` | mqttx `sub` parser supports compact qos tokens and `--output-mode default` branch. |
| `test_client_cli_mqttx_sub_delimiter_without_value_uses_default` | `[test_client][cli]` | mqttx `sub` parser applies default newline delimiter when `--delimiter` has no explicit value. |
| `test_client_cli_mqttx_sub_boolean_flags_without_values_enable_true` | `[test_client][cli]` | mqttx `sub` parser enables no-local and retain-as-published when boolean flags are passed without explicit values. |
| `test_client_cli_mqttx_sub_rejects_secure_tls_flags` | `[test_client][cli]` | mqttx `sub` parser rejects secure TLS flags with unsupported-option error. |
| `test_client_cli_bench_pub_extended_option_sweep_covers_parser_branches` | `[test_client][cli]` | bench `pub` parser sweep covers interval/message-interval, qos forms, metrics, protocol handling, split delimiter and many alias options. |
| `test_client_cli_bench_sub_and_conn_user_property_paths_are_supported` | `[test_client][cli]` | bench `sub` and `conn` paths cover user-property routing and boolean parsing branches. |
| `test_client_cli_bench_and_sub_boolean_parsers_reject_invalid_literals` | `[test_client][cli]` | invalid boolean literals for no-local/retain-as-published are rejected in bench/sub parsers. |
| `test_client_cli_sub_extended_connection_options_are_supported` | `[test_client][cli]` | mqttx `sub` parser accepts extended connection/session/output options and maps all related overrides. |
| `test_client_cli_simulate_extended_option_sweep_covers_parser_branches` | `[test_client][cli]` | simulate parser sweep covers publish/connection/will/schema aliases, metrics flag and selector mapping behavior. |

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
| `test_client_scenario_command_step32_mass_connect_mode_succeeds_with_fake_broker` | `[test_client][scenario]` | Step 32 `mass-connect` mode succeeds against a local fake MQTT broker and covers direct publish operation path. |
| `test_client_scenario_command_step32_publish_rate_mode_succeeds_with_fake_broker` | `[test_client][scenario]` | Step 32 `publish-rate` mode succeeds against a local fake broker, including persistent QoS2 publish handshake and publish property encoding paths. |
| `test_client_scenario_command_step32_multi_subscribe_mode_succeeds_with_fake_broker` | `[test_client][scenario]` | Step 32 `multi-subscribe` mode succeeds against a local fake broker and covers subscribe option mapping and subscriber completion path. |
| `test_client_scenario_command_step32_publish_rate_mode_qos1_succeeds_with_fake_broker` | `[test_client][scenario]` | Step 32 `publish-rate` mode with QoS1 succeeds against fake broker and covers persistent publish Puback handshake path. |
| `test_client_scenario_command_step32_mass_connect_mode_qos2_succeeds_with_fake_broker` | `[test_client][scenario]` | Step 32 `mass-connect` mode with QoS2 succeeds against fake broker and covers direct publish Pubrec/Pubrel/Pubcomp handshake path. |
| `test_client_scenario_command_step32_multi_subscribe_rejects_invalid_bench_settings` | `[test_client][scenario]` | Step 32 `multi-subscribe` mode validates bench settings and rejects invalid qos/retain-handling/subscription-identifier combinations. |
| `test_client_scenario_command_step32_publish_rate_mode_fails_when_broker_disconnects_on_publish` | `[test_client][scenario]` | Step 32 `publish-rate` mode returns non-zero when broker disconnects immediately after receiving a publish frame. |
