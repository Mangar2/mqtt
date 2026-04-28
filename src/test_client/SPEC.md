# test_client — Step 27-32 Test Client Shell

## Purpose

Provide a standalone executable shell for the MQTT 5.0 client test tooling.
This module owns profile persistence, command-line parsing, and connection-shell
orchestration for broker-supported transports (`mqtt`, `ws`) without TLS.

## Scope (Step 27-32)

- Persistent connection profiles with deterministic load/save behavior.
- Command-line subcommands for `connect`, `publish`, `subscribe`,
  `scenario`, `save-profile`, and `show-profile`.
- Top-level `--help` and `--version` command-surface support.
- mqttx compatibility command stubs for `conn`, `simulate`, `ls`,
  `init`, and `check` with help/discoverability flow.
- One-shot `publish` command with QoS-aware ACK completion flow.
- Long-running `subscribe` command with per-subscription MQTT 5 options and
  automation-oriented output pipeline controls.
- Effective-profile composition: defaults + optional profile file + CLI overrides.
- Validation of profile constraints before execution.
- Connection shell that negotiates MQTT CONNECT and keeps the session open until
  process signal (`SIGINT`/`SIGTERM`) with keep-alive pings.
- Reconnect retry loop driven by profile settings.
- One-shot `publish` command applies reconnect retry semantics driven by
  `reconnect_period_ms` and `maximum_reconnect_times`.
- Step 32 direct benchmark operations apply reconnect retry semantics driven by
  `reconnect_period_ms` and `maximum_reconnect_times`.
- Step 28 MQTT 5 CONNECT completeness: session/receive/packet-size/topic-alias
  properties, response/problem info flags, connect user properties, optional
  enhanced authentication properties, and full Last-Will property set.
- Step 29 publish matrix: payload source modes (inline/stdin/multiline/file),
  payload encodings (`raw`, `json`, `hex`, `base64`, `binary`, `protobuf`,
  `avro`), and MQTT 5 PUBLISH properties (format indicator, expiry, alias,
  response topic, correlation data, subscription identifier, content type,
  user properties).
- Step 29 publish runtime supports `-S/--payload-size` random payload
  generation and validates protobuf/avro schema options
  (`-Pp/--protobuf-path`, `-Pmn/--protobuf-message-name`,
  `-Ap/--avsc-path`) when matching payload encoding is selected.
- Step 30 subscribe matrix: one-or-many subscription entries with per-entry
  QoS/no-local/retain-as-published/retain-handling options, MQTT 5 subscribe
  properties (subscription identifier and user properties), and output modes
  (`clean`, template format, delimiter, optional append/truncate file sink,
  verbose packet log) with optional message-limit/timeout controls.
- mqttx `sub` command path is implemented with alias mapping for subscribe
  filter/options/output controls (`-q`, `-nl`, `-rap`, `-rh`, `-si`,
  `--output-mode`, `--file-write`, `--file-save`, `--delimiter`, `-f`).
- Step 31 scripted scenario workflows: built-in scenario catalog discovery,
  scenario selection, step-by-step pass/fail logging, and non-zero process exit
  when any scenario step fails.
- Step 32 load modes: `mass-connect`, `publish-rate`, and
  `multi-subscribe` with configurable connection count, connect/message
  interval, publish limit, topic/client templates, and optional JSON metrics
  output for automation tooling.
- Step 32 `publish-rate` uses persistent benchmark connections; `--count`
  controls pool size, `--interval` controls connection setup spacing,
  `--message-interval` controls per-publish delay, and `--limit 0` enables
  unlimited publish loop behavior.
- Step 32 bench publish applies `--split` payload tokenization and
  `-S/--payload-size` payload generation semantics.
- Step 32 bench publish applies publish properties (`dup`, payload-format,
  expiry, alias, response-topic, correlation-data, subscription-identifier,
  content-type, and user-properties) and payload encoding semantics.
- Step 32 `multi-subscribe` applies bench subscribe semantics for
  qos/no-local/retain-as-published/retain-handling/subscription-identifier.

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
- `session_expiry_interval_seconds`, `receive_maximum`, `maximum_packet_size`,
  `topic_alias_maximum`
- `request_response_information`, `request_problem_information`
- repeatable `connect_user_property`, `authentication_method`,
  `authentication_data`
- will keys: `will_topic`, `will_payload`, `will_qos`, `will_retain`,
  `will_delay_interval_seconds`, `will_payload_format_indicator`,
  `will_message_expiry_interval_seconds`, `will_content_type`,
  `will_response_topic`, `will_correlation_data`, repeatable
  `will_user_property`
- publish keys: `publish_topic`, `publish_qos`, `publish_retain`, `publish_dup`,
  `publish_payload`, `publish_payload_stdin`,
  `publish_payload_stdin_multiline`, `publish_payload_file`,
  `publish_payload_encoding`, `publish_payload_format_indicator`,
  `publish_message_expiry_interval_seconds`, `publish_topic_alias`,
  `publish_response_topic`, `publish_correlation_data`,
  `publish_correlation_data_encoding`, `publish_subscription_identifier`,
  `publish_content_type`, repeatable `publish_user_property`
- subscribe keys: repeatable `subscribe_entry`
  (`filter|qos|no_local|retain_as_published|retain_handling`),
  `subscribe_identifier`, repeatable `subscribe_user_property`,
  `subscribe_payload_format`, `subscribe_protobuf_path`,
  `subscribe_protobuf_message_name`, `subscribe_avsc_path`,
  `subscribe_clean_output`, `subscribe_verbose_packets`,
  `subscribe_output_file`, `subscribe_output_file_save`,
  `subscribe_output_append`,
  `subscribe_output_delimiter`, `subscribe_output_format`,
  `subscribe_message_limit`, `subscribe_wait_timeout_ms`
- `reconnect_period_ms`, `maximum_reconnect_times`

### `test_client_cli.h`

- `enum class TestClientCommand`
- `struct TestClientCliOptions`
- `parse_test_client_cli(int argc, const char* argv[])`
- `test_client_help_text()`
- `test_client_version_text()`

Compatibility behavior:

- `publish` command accepts alias `pub`.
- `subscribe` command accepts alias `sub`.
- Top-level `--version` and `-v` return version output without profile loading.
- Supported mqttx-style aliases are accepted for implemented capabilities,
  including short publish flags (`-t`, `-m`, `-q`, `-r`, `-d`, `-s`, `-M`),
  publish payload/schema flags (`-f`, `-Pp`, `-Pmn`, `-Ap`, `-S`),
  publish MQTT 5 properties (`-pf`, `-e`, `-ta`, `-rt`, `-cd`, `-up`, `-si`,
  `-ct`), connection aliases (`-h`, `-p`, `-i`, `--no-clean`, `-k`, `-u`,
  `-P`, `-l`, `--path`, `-wh`, `-rp`, `-se`, `--rcv-max`, `--req-response-info`,
  `--no-req-problem-info`, `-Cup`, `-am`), and will aliases (`-Wt`..`-Wup`).
- `-V/--mqtt-version` is accepted only for `5` or `5.0`.
- mqttx compatibility alias `--maximun-reconnect-times` is accepted and mapped
  to `maximum_reconnect_times`.
- mqttx-compatible command paths explicitly reject recognized-but-unimplemented
  `--debug`, `--save-options`, and `--load-options` with argument errors.
- Bench `-v/--verbose` enables verbose bench traces and does not imply
  metrics-json output.
- mqttx top-level commands `conn`, `simulate`, `ls`, `init`, and
  `check` are recognized as compatibility stubs: `--help` succeeds and
  non-help execution returns a not-implemented argument error.
- `bench --help` and `bench <subcommand> --help` (`conn|pub|sub`) are
  accepted and routed to help output.

### `test_client_scenario_runner.h`

- `list_test_client_scenarios()`
- `run_test_client_scenario_command(const TestClientCliOptions&, const TestClientProfile&, const std::string&)`

## Executable

`src/test_client_main.cpp` defines the `yahatestclient` entry point.

Behavior:

- `connect`: applies profile, connects with MQTT 5.0 handshake, keeps session
  open, and disconnects on signal.
- `publish`: applies profile, connects, publishes one message with selected
  QoS/payload/property matrix, waits for QoS completion (`PUBACK` /
  `PUBREC`→`PUBREL`→`PUBCOMP`), then disconnects.
- `subscribe`: applies profile, connects, subscribes using one or more
  subscription entries, emits received publishes through the configured output
  pipeline, acknowledges inbound QoS 1/2 handshakes, and exits on signal or
  optional message-limit condition.
- `scenario`: runs a selected built-in scenario with step-by-step status lines
  and non-zero exit on the first failed step, or runs a Step 32 load mode;
  `--list-scenarios` prints the built-in scenario catalog and available
  load modes.
- `save-profile`: writes deterministic key/value profile file.
- `show-profile`: prints effective profile (password redacted).

## Constraints

- MQTT version is fixed to 5.0.
- Supported transports are `mqtt` and `ws` only.
- TLS-related options are intentionally unsupported.
- Unknown profile keys and unknown CLI options are rejected as errors.
- Unknown commands are rejected with explicit parser errors.

## Validation

Unit tests for profile parsing/persistence, CLI parsing, and scenario-runner
catalog/execution paths live in
`test/` and are specified in `test/TEST_SPEC.md`.
