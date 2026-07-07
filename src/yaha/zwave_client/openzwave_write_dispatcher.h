#pragma once

#include "yaha/zwave_devices/zwave_devices_mapper.h"

#include <cstdint>
#include <string>
#include <variant>

namespace OpenZWave {
class Manager;
} // namespace OpenZWave

namespace yaha {

class OpenZwaveWriteDispatcher final {
public:
    struct WriteMetadata {
        std::string normalizedType{};
        std::uint8_t valueTypeCode{0U};
        std::uint8_t genreCode{1U};
        std::string valueTypeName{};
        std::string payloadText{};
    };

    [[nodiscard]] static WriteMetadata buildWriteMetadata(
        const ZwaveResolvedId& target,
        std::uint8_t genreCode,
        const std::variant<bool, double, std::string>& value);

    [[nodiscard]] static bool write(
        OpenZWave::Manager& manager,
        std::uint32_t homeId,
        const ZwaveResolvedId& target,
        const WriteMetadata& metadata,
        const std::variant<bool, double, std::string>& value);

    static void setConfigParam(
        OpenZWave::Manager& manager,
        std::uint32_t homeId,
        std::uint16_t nodeId,
        std::uint16_t paramId,
        double value);
};

} // namespace yaha
