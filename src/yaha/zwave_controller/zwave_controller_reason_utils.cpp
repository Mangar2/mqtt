#include "yaha/zwave_controller/zwave_controller_reason_utils.h"

namespace yaha::zwave_controller_reason_utils {

bool isEnablePollAllowedClass(const std::uint16_t classId) {
    return std::ranges::find(kEnablePollAllowedClasses, classId) != kEnablePollAllowedClasses.end();
}

std::string buildZwaveNetworkReason(const std::uint16_t nodeId, const std::optional<std::uint64_t>& valueId) {
    std::string reason = "received from zwave network node: " + std::to_string(nodeId);
    if (!valueId.has_value()) {
        return reason;
    }
    return reason + " id: " + std::to_string(*valueId);
}

std::string buildValueEventCommunicationReason(
    const ZwaveControllerValueEvent& event,
    const std::string_view sourceName) {
    std::string reason = "node " + std::to_string(event.nodeId)
        + " communication succeeded; source=" + std::string{sourceName}
        + " target=node/" + std::to_string(event.nodeId)
        + "/class/" + std::to_string(event.classId)
        + "/instance/" + std::to_string(event.instance)
        + "/index/" + std::to_string(event.index);

    if (event.valueId.has_value()) {
        reason += " valueId=" + std::to_string(*event.valueId);
    } else {
        reason += " valueId=unknown";
    }

    return reason;
}

} // namespace yaha::zwave_controller_reason_utils
