#pragma once

#include "yaha/zwave_controller/zwave_controller.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace yaha::zwave_controller_reason_utils {

inline constexpr std::array<std::uint16_t, 2> kEnablePollAllowedClasses{
    kZwaveSwitchBinaryClass, // COMMAND_CLASS_SWITCH_BINARY (0x25)
    kZwaveSwitchMultilevelClass // COMMAND_CLASS_SWITCH_MULTILEVEL (0x26)
};

[[nodiscard]] bool isEnablePollAllowedClass(std::uint16_t classId);
[[nodiscard]] std::string buildZwaveNetworkReason(std::uint16_t nodeId, const std::optional<std::uint64_t>& valueId);
[[nodiscard]] std::string buildValueEventCommunicationReason(
    const ZwaveControllerValueEvent& event,
    std::string_view sourceName);

} // namespace yaha::zwave_controller_reason_utils
