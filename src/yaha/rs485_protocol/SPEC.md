# rs485_protocol

Phase 2 scope in this module:
- serial frame codec for RS485 protocol versions 0 and 1
- CRC16/parity calculation helpers used by encode/decode
- stream-reader behavior with noise skipping and per-frame error reporting

## Public API

### Struct Rs485SerialMessage

Fields:
- sender (0..127 expected for decoded frames)
- receiver (0..127 expected for decoded frames)
- reply (flag bit 0)
- command (single ASCII command byte)
- value (decoded message value; `h`/`t`/`s` use `high + low/100`)
- parity (v0 parity byte)
- crc16 (v1 crc16)
- version (0 or 1)
- length (7 for v0, 9 for v1)

### Struct Rs485ReadResult

Fields:
- startIndex (next decode start position)
- message (decoded message for success cases)
- hex (hex dump from decode start)
- error (empty on success)

### Functions

- `calcRs485Crc16(...)`: CRC16 CCITT (`0xFFFF`, polynomial `0x1021`)
- `calcRs485Parity(...)`: XOR parity over selected byte range
- `decodeRs485SerialMessage(...)`: return one decoded frame from byte stream position
- `encodeRs485SerialMessage(...)`: encode one frame to bytes
- `Rs485StreamReader::read(...)`: parse all frames from one chunk including noise and errors

## Behavior notes

- Decode validates sender/receiver range, supported version, length, and CRC/parity.
- Version 0 length is fixed to 7 bytes.
- Version 1 length is read from byte 4 and must be exactly 9.
- Stream reader skips noise bytes `0` and values `>127` before each decode attempt.
- On decode error, reader advances by the current message length from parser state, preserving legacy behavior.
- All parse/encode failures throw `YahaError`, and `Rs485StreamReader::read` stores `YahaError::buildMessage()` output in `Rs485ReadResult.error`.
- Unsupported encode versions throw a deterministic error.
