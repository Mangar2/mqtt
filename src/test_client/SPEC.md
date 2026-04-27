# test_client — Step 27 Test Client Shell

## Purpose

Provide a standalone executable shell for the MQTT 5.0 client test tooling.
This module owns profile persistence, command-line parsing, and connection-shell
orchestration for broker-supported transports (`mqtt`, `ws`) without TLS.

## Scope (Step 27)

- Persistent connection profiles with deterministic load/save behavior.
- Command-line subcommands for `connect`, `save-profile`, `show-profile`.
- Effective-profile composition: defaults + optional profile file + CLI overrides.
- Validation of profile constraints before execution.
- Connection shell that negotiates MQTT CONNECT and keeps the session open until
  process signal (`SIGINT`/`SIGTERM`) with keep-alive pings.
- Reconnect retry loop driven by profile settings.

## Public API

### `test_client_profile.h`

- `enum class TestClientTransport { Mqtt, Ws }`
- `struct TestClientProfile`
- `to_string(TestClientTransport)`
- `transport_from_string(std::string_view)`
- `validate_test_client_profile_or_throw(const TestClientProfile&)`
- `apply_profile_override(TestClientProfile&, std::string_view key, std::string_view value)`
- `load_test_client_profile_from_file(const std::string&)`
- `save_test_client_profile_to_file(const std::string&, const TestClientProfile&)`

Profile keys:

- `host`, `port`, `transport`
- `ws_path`, repeatable `ws_header`
- `client_id`, `clean_start`, `keep_alive_seconds`
- `username`, `password`
- `reconnect_period_ms`, `maximum_reconnect_times`

### `test_client_cli.h`

- `enum class TestClientCommand`
- `struct TestClientCliOptions`
- `parse_test_client_cli(int argc, const char* argv[])`
- `test_client_help_text()`

## Executable

`src/test_client_main.cpp` defines the `yahatestclient` entry point.

Behavior:

- `connect`: applies profile, connects with MQTT 5.0 handshake, keeps session
  open, and disconnects on signal.
- `save-profile`: writes deterministic key/value profile file.
- `show-profile`: prints effective profile (password redacted).

## Constraints

- MQTT version is fixed to 5.0.
- Supported transports are `mqtt` and `ws` only.
- TLS-related options are intentionally unsupported.
- Unknown profile keys and unknown CLI options are rejected as errors.

## Validation

Unit tests for profile parsing/persistence and CLI parsing live in
`test/` and are specified in `test/TEST_SPEC.md`.
