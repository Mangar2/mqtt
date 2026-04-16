#pragma once

/**
 * @file read_buffer.h
 * @brief Read-only buffer cursor for MQTT codec decode operations.
 */

#include <cstddef>
#include <cstdint>
#include <span>
#include "codec/codec_error.h"

namespace mqtt {

/**
 * @brief A read-only cursor over a contiguous byte span.
 *
 * Decode functions advance the cursor as bytes are consumed.  On underflow
 * a `CodecException` with `CodecError::BufferTooShort` is thrown.
 *
 * The underlying span must outlive this object.
 */
class ReadBuffer {
public:
    /**
     * @brief Constructs a ReadBuffer positioned at byte 0 of @p data.
     * @param data Byte span to read from; must remain valid for the lifetime of this object.
     */
    explicit ReadBuffer(std::span<const uint8_t> data) noexcept
        : data_(data), pos_(0)
    {}

    /**
     * @brief Returns the number of bytes not yet consumed.
     */
    [[nodiscard]] std::size_t remaining() const noexcept
    {
        return data_.size() - pos_;
    }

    /**
     * @brief Returns the current read position (bytes consumed so far).
     */
    [[nodiscard]] std::size_t position() const noexcept { return pos_; }

    /**
     * @brief Returns `true` if at least @p n bytes remain in the buffer.
     * @param n Minimum number of bytes required.
     */
    [[nodiscard]] bool has_remaining(std::size_t n = 1) const noexcept
    {
        return remaining() >= n;
    }

    /**
     * @brief Reads and returns the next byte, advancing the cursor by 1.
     * @return The consumed byte.
     * @throws CodecException(BufferTooShort) if no bytes remain.
     */
    [[nodiscard]] uint8_t read_byte()
    {
        if (pos_ >= data_.size()) {
            throw CodecException{CodecError::BufferTooShort,
                "Buffer too short: expected 1 byte"};
        }
        return data_[pos_++];
    }

    /**
     * @brief Reads @p n bytes from the buffer and advances the cursor by @p n.
     * @param n Number of bytes to consume.
     * @return Span covering the consumed bytes (valid as long as the source span is alive).
     * @throws CodecException(BufferTooShort) if fewer than @p n bytes remain.
     */
    [[nodiscard]] std::span<const uint8_t> read_bytes(std::size_t n)
    {
        if (remaining() < n) {
            throw CodecException{CodecError::BufferTooShort,
                "Buffer too short: not enough bytes remaining"};
        }
        auto s = data_.subspan(pos_, n);
        pos_ += n;
        return s;
    }

private:
    std::span<const uint8_t> data_;  ///< Underlying byte span.
    std::size_t              pos_;   ///< Current read offset.
};

} // namespace mqtt
