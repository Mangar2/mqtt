#pragma once

/**
 * @file primitive_codec.h
 * @brief MQTT 5.0 primitive type encoder / decoder (Module 2.1).
 *
 * All encode functions append bytes to a `WriteBuffer`.
 * All decode functions consume bytes from a `ReadBuffer`; the cursor advances
 * past decoded data.
 */

#include "codec/read_buffer.h"
#include "codec/write_buffer.h"
#include "data_model/types/binary_data.h"
#include "data_model/types/integers.h"
#include "data_model/types/utf8_string.h"
#include "data_model/types/variable_byte_integer.h"
#include <cstdint>

namespace mqtt {

//  Encoding

/**
 * @brief Appends a single byte to @p buf.
 * @param buf  Destination buffer.
 * @param byte Value to append.
 */
void encode_byte(WriteBuffer &buf, uint8_t byte);

/**
 * @brief Encodes a Variable Byte Integer and appends 1–4 bytes to @p buf.
 *
 * The MQTT VBI encoding stores 7 data bits per byte; the MSB of each byte is
 * the continuation flag (Section 1.5.5).
 *
 * @param buf  Destination buffer.
 * @param vbi  Value to encode.
 * @throws CodecException(VariableByteIntegerOverflow) if `vbi.value` exceeds
 *         `VariableByteInteger::k_max_value` (268 435 455).
 */
void encode_variable_byte_integer(WriteBuffer &buf, VariableByteInteger vbi);

/**
 * @brief Encodes a Two Byte Integer in big-endian order and appends 2 bytes to
 * @p buf.
 * @param buf   Destination buffer.
 * @param value Value to encode.
 */
void encode_two_byte_integer(WriteBuffer &buf, TwoByteInteger value);

/**
 * @brief Encodes a Four Byte Integer in big-endian order and appends 4 bytes to
 * @p buf.
 * @param buf   Destination buffer.
 * @param value Value to encode.
 */
void encode_four_byte_integer(WriteBuffer &buf, FourByteInteger value);

/**
 * @brief Encodes a UTF-8 string (2-byte big-endian length prefix + UTF-8 bytes)
 * to @p buf.
 * @param buf Destination buffer.
 * @param str String to encode.
 * @throws CodecException(StringTooLong) if `str.value.size()` exceeds
 *         `Utf8String::k_max_byte_length` (65 535).
 */
void encode_utf8_string(WriteBuffer &buf, const Utf8String &str);

/**
 * @brief Encodes a UTF-8 string pair (name then value) to @p buf.
 * @param buf  Destination buffer.
 * @param pair String pair to encode.
 * @throws CodecException(StringTooLong) if either string exceeds 65 535 bytes.
 */
void encode_utf8_string_pair(WriteBuffer &buf, const Utf8StringPair &pair);

/**
 * @brief Encodes Binary Data (2-byte big-endian length prefix + raw bytes) to
 * @p buf.
 * @param buf  Destination buffer.
 * @param data Binary data to encode.
 * @throws CodecException(StringTooLong) if `data.data.size()` exceeds
 *         `BinaryData::k_max_byte_length` (65 535).
 */
void encode_binary_data(WriteBuffer &buf, const BinaryData &data);

//  Decoding

/**
 * @brief Reads one byte from @p buf, advancing the cursor by 1.
 * @param buf Source buffer.
 * @return The decoded byte.
 * @throws CodecException(BufferTooShort) if no bytes remain.
 */
[[nodiscard]] uint8_t decode_byte(ReadBuffer &buf);

/**
 * @brief Decodes a Variable Byte Integer from @p buf, consuming 1–4 bytes.
 * @param buf Source buffer.
 * @return The decoded VariableByteInteger.
 * @throws CodecException(BufferTooShort)             on truncated input.
 * @throws CodecException(VariableByteIntegerOverflow) if the continuation bit
 * is set in byte 4.
 */
[[nodiscard]] VariableByteInteger decode_variable_byte_integer(ReadBuffer &buf);

/**
 * @brief Decodes a Two Byte Integer (big-endian) from @p buf, consuming 2
 * bytes.
 * @param buf Source buffer.
 * @return The decoded TwoByteInteger.
 * @throws CodecException(BufferTooShort) on truncated input.
 */
[[nodiscard]] TwoByteInteger decode_two_byte_integer(ReadBuffer &buf);

/**
 * @brief Decodes a Four Byte Integer (big-endian) from @p buf, consuming 4
 * bytes.
 * @param buf Source buffer.
 * @return The decoded FourByteInteger.
 * @throws CodecException(BufferTooShort) on truncated input.
 */
[[nodiscard]] FourByteInteger decode_four_byte_integer(ReadBuffer &buf);

/**
 * @brief Decodes a UTF-8 string (2-byte length prefix + bytes) from @p buf.
 * @param buf Source buffer.
 * @return The decoded Utf8String.
 * @throws CodecException(BufferTooShort) on truncated input.
 */
[[nodiscard]] Utf8String decode_utf8_string(ReadBuffer &buf);

/**
 * @brief Decodes a UTF-8 string pair (name string followed by value string)
 * from @p buf.
 * @param buf Source buffer.
 * @return The decoded Utf8StringPair.
 * @throws CodecException(BufferTooShort) on truncated input.
 */
[[nodiscard]] Utf8StringPair decode_utf8_string_pair(ReadBuffer &buf);

/**
 * @brief Decodes Binary Data (2-byte length prefix + raw bytes) from @p buf.
 * @param buf Source buffer.
 * @return The decoded BinaryData.
 * @throws CodecException(BufferTooShort) on truncated input.
 */
[[nodiscard]] BinaryData decode_binary_data(ReadBuffer &buf);

} // namespace mqtt
