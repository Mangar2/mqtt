#pragma once

/**
 * @file serial_device_parser.h
 * @brief Stateful stream parser for legacy SerialDevice serial payloads.
 */

#include "yaha/serial_device/serial_device_message.h"

#include <optional>
#include <string>
#include <string_view>

namespace yaha {

/**
 * @brief Parses chunked serial input into normalized SerialDevice messages.
 */
class SerialDeviceStreamParser {
public:
    /**
     * @brief Feeds one serial text chunk and returns at most one parsed message.
     * @param chunkText Input serial chunk.
     * @return One parsed message or std::nullopt when no full/known frame is available.
     */
    [[nodiscard]] std::optional<SerialDeviceMessage> parseChunk(std::string_view chunkText);

private:
    std::string receivedDataAsString_{};
};

} // namespace yaha
