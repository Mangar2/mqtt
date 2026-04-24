#pragma once

/**
 * @file crash_safe_file.h
 * @brief Atomic, crash-safe file write and read with CRC-32 integrity checking
 * (Module 13).
 *
 * Uses a two-phase atomic rename strategy so that a crash during a write never
 * leaves the broker without a recoverable state:
 *
 * **Write sequence:**
 * 1. Serialize payload + CRC-32 footer into `<stem>.tmp`.
 * 2. Rename `<stem>.dat` → `<stem>.bak`  (preserves last good copy).
 * 3. Rename `<stem>.tmp` → `<stem>.dat`  (atomic promotion on POSIX / NTFS).
 *
 * **Read sequence (startup recovery):**
 * `read_latest()` probes `<stem>.dat`, then `<stem>.bak`, then `<stem>.tmp`.
 * The first file whose CRC-32 matches is returned; the others are ignored.
 * If no file is valid, `std::nullopt` is returned (first boot / unrecoverable).
 *
 * **File format (all integers little-endian):**
 * ```
 * [magic   : 4 bytes]  "MQTT"
 * [version : 1 byte ]  0x01
 * [count   : 4 bytes]  number of records
 * [records : variable]
 * [crc32   : 4 bytes]  CRC-32/ISO-HDLC of all preceding bytes
 * ```
 */

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace mqtt {

/**
 * @brief Atomic, crash-safe file I/O with CRC-32 integrity protection (Module
 * 13).
 *
 * Not thread-safe — external synchronisation is required when shared across
 * threads.
 */
class CrashSafeFile {
public:
  /**
   * @brief Magic bytes placed at the start of every file produced by this class.
   */
  static constexpr uint8_t k_magic[4] = {'M', 'Q', 'T', 'T'};
  /**
   * @brief Format version encoded in byte 4 of every file.
   */
  static constexpr uint8_t k_format_version = 0x01U;

  /**
   * @brief Construct a CrashSafeFile operating under directory @p dir.
   *
   * The constructor does not create or open any file.
   *
   * @param dir   Directory that holds the data files.
   * @param stem  Base name used for `<stem>.dat`, `<stem>.bak`, `<stem>.tmp`.
   */
  CrashSafeFile(std::filesystem::path dir, std::string stem);

  /**
   * @brief Write @p records atomically to durable storage.
   *
   * Builds the binary payload (magic + version + count + serialized records +
   * CRC-32), writes it to `<stem>.tmp`, then performs the two-phase rename.
   *
   * @param records  Serialized record bytes; produced by the caller using
   *                 `record_codec` helpers.  The count is deduced from
   *                 @p record_count.
   * @param record_count  Number of logical records encoded in @p records.
   * @throws PersistenceException(WriteFailure) on any I/O error.
   */
  void write(const std::vector<uint8_t> &records, uint32_t record_count);

  /**
   * @brief Find and return the latest valid file on startup.
   *
   * Probes `<stem>.dat`, `<stem>.bak`, then `<stem>.tmp` in order.
   * Returns a vector whose first element is the `record_count` (cast to
   * `uint32_t`) and remaining bytes are the raw record payload, or
   * `std::nullopt` if no valid file exists.
   *
   * @return Pair of (record_count, record_bytes), or `std::nullopt`.
   * @throws PersistenceException(ReadFailure) on unexpected I/O errors.
   */
  [[nodiscard]] std::optional<std::pair<uint32_t, std::vector<uint8_t>>> read_latest() const;

  /**
   * @brief Delete all files managed by this instance (`dat`, `bak`, `tmp`).
   *
   * Silently ignores files that do not exist.  Used by the Session
   * Persistence Adapter to implement `delete_session`.
   *
   * @throws PersistenceException(WriteFailure) if a present file cannot be
   *         removed.
   */
  void remove_all();

  /**
   * @brief Compute CRC-32/ISO-HDLC of an arbitrary byte range.
   *
   * Exposed as a static helper so callers can independently verify checksums
   * in tests.
   *
   * @param data  Byte range to checksum.
   * @return 32-bit CRC value.
   */
  [[nodiscard]] static uint32_t crc32(std::span<const uint8_t> data) noexcept;

private:
  std::filesystem::path dir_; ///< Directory holding the data files.
  std::string stem_;          ///< Base name for `.dat`, `.bak`, `.tmp`.

  /**
   * @brief Return full path for one managed file suffix.
   * @param suffix File suffix, for example ".dat".
   * @return Full path in configured directory.
   */
  [[nodiscard]] std::filesystem::path path_for(std::string_view suffix) const;

  /**
   * @brief Try to read and validate @p path.
   *
   * Returns a (record_count, record_bytes) pair when the file exists, can be
   * read, and its CRC-32 footer matches.  Returns `std::nullopt` otherwise
   * without throwing.
   */
  [[nodiscard]] static std::optional<std::pair<uint32_t, std::vector<uint8_t>>> try_read(const std::filesystem::path &path);
};

} // namespace mqtt
