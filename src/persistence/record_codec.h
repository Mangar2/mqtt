#pragma once

/**
 * @file record_codec.h
 * @brief Low-level binary serialization helpers for the persistence module
 * (Module 13).
 *
 * All integers are written/read in **little-endian** byte order.
 * Strings are prefixed with a `uint16_t` length field followed by UTF-8 bytes.
 * Binary blobs are prefixed with a `uint32_t` length field followed by raw
 * bytes. Booleans occupy one `uint8_t` (0 = false, non-zero = true).
 *
 * All write helpers append to a `std::vector<uint8_t>` buffer.
 * All read helpers advance a `std::span<const uint8_t>` cursor.
 */

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "data_model/types/binary_data.h"
#include "data_model/types/utf8_string.h"
#include "persistence/persistence_error.h"

namespace mqtt::record_codec {

// ── Write helpers
// ─────────────────────────────────────────────────────────────

/**
 * @brief Append a single byte to @p buf.
 * @param buf  Destination buffer.
 * @param val  Byte value to append.
 */
inline void write_u8(std::vector<uint8_t> &buf, uint8_t val) {
  buf.push_back(val);
}

/**
 * @brief Append a little-endian 16-bit integer to @p buf.
 * @param buf  Destination buffer.
 * @param val  Value to append.
 */
inline void write_u16(std::vector<uint8_t> &buf, uint16_t val) {
  buf.push_back(static_cast<uint8_t>(val & 0xFFU));
  buf.push_back(static_cast<uint8_t>((val >> 8U) & 0xFFU));
}

/**
 * @brief Append a little-endian 32-bit integer to @p buf.
 * @param buf  Destination buffer.
 * @param val  Value to append.
 */
inline void write_u32(std::vector<uint8_t> &buf, uint32_t val) {
  buf.push_back(static_cast<uint8_t>(val & 0xFFU));
  buf.push_back(static_cast<uint8_t>((val >> 8U) & 0xFFU));
  buf.push_back(static_cast<uint8_t>((val >> 16U) & 0xFFU));
  buf.push_back(static_cast<uint8_t>((val >> 24U) & 0xFFU));
}

/**
 * @brief Append a boolean as a single byte (0 or 1) to @p buf.
 * @param buf  Destination buffer.
 * @param val  Boolean value.
 */
inline void write_bool(std::vector<uint8_t> &buf, bool val) {
  buf.push_back(val ? 1U : 0U);
}

/**
 * @brief Append a UTF-8 string as `uint16_t length + bytes` to @p buf.
 * @param buf  Destination buffer.
 * @param str  String to append; length must not exceed 65 535 bytes.
 * @throws PersistenceException(CorruptRecord) if the string exceeds the limit.
 */
inline void write_string(std::vector<uint8_t> &buf, std::string_view str) {
  if (str.size() > Utf8String::k_max_byte_length) {
    throw PersistenceException(PersistenceError::CorruptRecord,
                               "String exceeds maximum UTF-8 length");
  }
  write_u16(buf, static_cast<uint16_t>(str.size()));
  buf.insert(buf.end(), str.begin(), str.end());
}

/**
 * @brief Append a Utf8String to @p buf.
 * @param buf  Destination buffer.
 * @param str  String to append.
 */
inline void write_utf8(std::vector<uint8_t> &buf, const Utf8String &str) {
  write_string(buf, str.value);
}

/**
 * @brief Append a BinaryData blob as `uint32_t length + bytes` to @p buf.
 * @param buf   Destination buffer.
 * @param data  Blob to append.
 */
inline void write_binary(std::vector<uint8_t> &buf, const BinaryData &data) {
  write_u32(buf, static_cast<uint32_t>(data.data.size()));
  buf.insert(buf.end(), data.data.begin(), data.data.end());
}

// ── Read helpers
// ──────────────────────────────────────────────────────────────

/**
 * @brief Read a single byte from @p cursor and advance it.
 * @param cursor  View into the remaining buffer; shrunk by 1 on success.
 * @return Byte read.
 * @throws PersistenceException(CorruptRecord) if the buffer is too short.
 */
inline uint8_t read_u8(std::span<const uint8_t> &cursor) {
  if (cursor.empty()) {
    throw PersistenceException(PersistenceError::CorruptRecord,
                               "Buffer too short reading uint8");
  }
  uint8_t val = cursor[0];
  cursor = cursor.subspan(1);
  return val;
}

/**
 * @brief Read a little-endian 16-bit integer from @p cursor and advance it.
 * @param cursor  View into the remaining buffer; shrunk by 2 on success.
 * @return Value read.
 * @throws PersistenceException(CorruptRecord) if the buffer is too short.
 */
inline uint16_t read_u16(std::span<const uint8_t> &cursor) {
  if (cursor.size() < 2U) {
    throw PersistenceException(PersistenceError::CorruptRecord,
                               "Buffer too short reading uint16");
  }
  uint16_t val = static_cast<uint16_t>(cursor[0]) |
                 static_cast<uint16_t>(static_cast<uint16_t>(cursor[1]) << 8U);
  cursor = cursor.subspan(2);
  return val;
}

/**
 * @brief Read a little-endian 32-bit integer from @p cursor and advance it.
 * @param cursor  View into the remaining buffer; shrunk by 4 on success.
 * @return Value read.
 * @throws PersistenceException(CorruptRecord) if the buffer is too short.
 */
inline uint32_t read_u32(std::span<const uint8_t> &cursor) {
  if (cursor.size() < 4U) {
    throw PersistenceException(PersistenceError::CorruptRecord,
                               "Buffer too short reading uint32");
  }
  uint32_t val = static_cast<uint32_t>(cursor[0]) |
                 (static_cast<uint32_t>(cursor[1]) << 8U) |
                 (static_cast<uint32_t>(cursor[2]) << 16U) |
                 (static_cast<uint32_t>(cursor[3]) << 24U);
  cursor = cursor.subspan(4);
  return val;
}

/**
 * @brief Read a boolean byte from @p cursor and advance it.
 * @param cursor  View into the remaining buffer; shrunk by 1 on success.
 * @return Boolean value.
 * @throws PersistenceException(CorruptRecord) if the buffer is too short.
 */
inline bool read_bool(std::span<const uint8_t> &cursor) {
  return read_u8(cursor) != 0U;
}

/**
 * @brief Read a length-prefixed UTF-8 string from @p cursor and advance it.
 * @param cursor  View into the remaining buffer; shrunk by 2 + length on
 * success.
 * @return String read.
 * @throws PersistenceException(CorruptRecord) if the buffer is too short.
 */
inline Utf8String read_utf8(std::span<const uint8_t> &cursor) {
  uint16_t len = read_u16(cursor);
  if (cursor.size() < len) {
    throw PersistenceException(PersistenceError::CorruptRecord,
                               "Buffer too short reading UTF-8 string");
  }
  Utf8String result;
  result.value.assign(reinterpret_cast<const char *>(cursor.data()), len);
  cursor = cursor.subspan(len);
  return result;
}

/**
 * @brief Read a length-prefixed binary blob from @p cursor and advance it.
 * @param cursor  View into the remaining buffer; shrunk by 4 + length on
 * success.
 * @return BinaryData read.
 * @throws PersistenceException(CorruptRecord) if the buffer is too short.
 */
inline BinaryData read_binary(std::span<const uint8_t> &cursor) {
  uint32_t len = read_u32(cursor);
  if (cursor.size() < len) {
    throw PersistenceException(PersistenceError::CorruptRecord,
                               "Buffer too short reading binary data");
  }
  BinaryData result;
  result.data.assign(cursor.begin(), cursor.begin() + len);
  cursor = cursor.subspan(len);
  return result;
}

} // namespace mqtt::record_codec
