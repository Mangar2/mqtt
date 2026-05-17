# rs485_protocol test specification

## Scope

Unit tests for RS485 phase-2 protocol codec and stream-reader behavior.

## Test cases

| Name | Scenario | Input | Expected |
|------|----------|-------|----------|
| `rs485_codec_roundtrip_version0_preserves_fields` | encode/decode parity frame v0 | one v0 message with sender/receiver/reply/command/value | decoded message equals encoded semantic fields |
| `rs485_codec_roundtrip_version1_preserves_fields` | encode/decode crc frame v1 | one v1 message with sender/receiver/reply/command/value | decoded message equals encoded semantic fields |
| `rs485_codec_decodes_fractional_commands_h_t_s` | command-specific decode value conversion | v1 frame with command `h` and high/low bytes | decoded value is `high + low/100` |
| `rs485_codec_rejects_crc_mismatch` | crc validation failure | valid v1 frame with one CRC byte corrupted | decode throws crc mismatch error |
| `rs485_codec_rejects_unsupported_version_encode` | unsupported encode version failure path | message with version `2` | encode throws deterministic error |
| `rs485_stream_reader_skips_noise_and_parses_multiple_messages` | noise handling and multi-frame parsing | noise prefix + one v0 + one v1 frame | two successful read results, no errors |
| `rs485_stream_reader_reports_error_and_continues_by_message_length` | legacy error advance behavior | invalid v1 frame followed by valid v0 frame | first result has error, second result decodes valid message |
