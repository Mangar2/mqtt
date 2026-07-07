# persistence — Module 13: Persistence Adapter

Writes and reads broker state to durable, crash-safe storage.
Depends on module 4 (In-Memory Store / data models).

## Crash Safety

All writes use a two-phase atomic rename strategy:

1. Serialize data + CRC32 footer into `<name>.tmp`.
2. Rename `<name>.dat` → `<name>.bak` (preserve last good copy).
3. Rename `<name>.tmp` → `<name>.dat` (atomic promotion).

Each rename is atomic on supported file systems (POSIX / NTFS).
A crash at any point leaves at least one intact, checksum-verified file.

## Startup Recovery

`CrashSafeFile::read_latest()` probes files in order:

| Priority | File       | Condition              |
|----------|------------|------------------------|
| 1        | `<name>.dat` | CRC32 matches          |
| 2        | `<name>.bak` | CRC32 matches          |
| 3        | `<name>.tmp` | CRC32 matches (rare)   |
| —        | (none)      | Returns `std::nullopt` |

If no file is valid the caller receives `std::nullopt` and the broker starts
with an empty in-memory state (first boot or unrecoverable corruption).

## Binary Record Format

```
[magic    : 4 bytes]  "MQTT"
[version  : 1 byte ]  0x01
[count    : 4 bytes]  number of records (little-endian uint32)
[record 0 : variable]
[record N : variable]
[crc32    : 4 bytes]  CRC-32/ISO-HDLC of all preceding bytes (little-endian)
```

All multi-byte integers are **little-endian**.
Strings are encoded as `uint16_t length` followed by UTF-8 bytes (no NUL).
Binary blobs are encoded as `uint32_t length` followed by raw bytes.
`bool` fields are encoded as a single `uint8_t` (0 = false, 1 = true).

## Sub-modules

| File(s)                                | Plan ref | Description                                 |
|----------------------------------------|----------|---------------------------------------------|
| `persistence_error.h`                  | 13       | `PersistenceError` enum + `PersistenceException` |
| `record_codec.h`                       | 13       | Low-level binary read/write helpers          |
| `crash_safe_file.h` / `.cpp`           | 13       | Atomic write + CRC32 + startup recovery      |
| `session_persistence.h` / `.cpp`       | 13.1     | Serialize / deserialize `SessionState`       |
| `retained_message_persistence.h`/`.cpp`| 13.2     | Serialize / deserialize retained `Message`s  |
| `inflight_persistence.h` / `.cpp`      | 13.3     | Serialize / deserialize `InflightEntry`s     |
| `offline_queue_persistence.h` / `.cpp` | 13.4     | Serialize / deserialize offline `OfflineQueue` entries |
