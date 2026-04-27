# test_client/test — Unit tests for Step 27 shell

## Profile Model

| Test case | Tag | Description |
|-----------|-----|-------------|
| `test_client_profile_defaults_validate` | `[test_client][profile]` | Default profile values are valid for Step 27 shell constraints. |
| `test_client_profile_save_and_load_roundtrip_preserves_values` | `[test_client][profile]` | Profile persistence roundtrip preserves configured host/transport/ws headers/auth/reconnect values. |
| `test_client_profile_load_rejects_unknown_key` | `[test_client][profile]` | Loading profile file with unsupported key fails with parse error. |
| `test_client_profile_validate_rejects_invalid_ws_path` | `[test_client][profile]` | WS transport profile requires `ws_path` starting with `/`. |
| `test_client_profile_validation_and_override_error_paths` | `[test_client][profile]` | Invalid host/port/client-id/timeout/auth values and malformed overrides are rejected. |
| `test_client_profile_transport_helpers_cover_all_variants` | `[test_client][profile]` | Transport string conversion/parsing covers mqtt/ws and invalid input branch. |

## CLI Parsing

| Test case | Tag | Description |
|-----------|-----|-------------|
| `test_client_cli_connect_parses_profile_and_overrides` | `[test_client][cli]` | `connect` command parses profile file option and field overrides correctly. |
| `test_client_cli_save_profile_requires_output` | `[test_client][cli]` | `save-profile` command without `--output` fails fast with clear argument error. |
| `test_client_cli_show_profile_parses_basic_options` | `[test_client][cli]` | `show-profile` command parses profile path and optional overrides. |
| `test_client_cli_rejects_unknown_option` | `[test_client][cli]` | Unknown CLI flags are rejected as argument errors. |
| `test_client_cli_save_profile_with_all_supported_options_succeeds` | `[test_client][cli]` | `save-profile` parses `--output` and all supported override flags successfully. |
| `test_client_cli_help_and_error_paths` | `[test_client][cli]` | Help mode, unknown command, and missing option values follow expected parser behavior. |
