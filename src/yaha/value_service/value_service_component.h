#pragma once

/**
 * @file value_service_component.h
 * @brief ValueService config contract for YAHA standalone runtime.
 */

#include "yaha/message/message.h"

#include <cstdint>
#include <string>

namespace yaha {

inline constexpr std::uint16_t kDefaultValueServiceFileStorePort = 8210U;

/**
 * @brief Runtime configuration for ValueService component.
 */
struct ValueServiceConfig {
    std::string monitorTopicPrefix{"$MONITOR/FileStore"}; ///< Monitoring topic prefix for FileStore events.
    std::string valuesKeyPath{"/valueservice/values"};     ///< FileStore key path for full value map.
    std::string fileStoreHost{"127.0.0.1"};               ///< FileStore HTTP host.
    std::uint16_t fileStorePort{kDefaultValueServiceFileStorePort}; ///< FileStore HTTP port.
    bool fileStoreEnabled{true};                            ///< Enables FileStore load/save behavior.
    Qos subscribeQos{Qos::AtLeastOnce};                     ///< QoS for subscriptions and outbound state publishes.
    std::string legacyValuesFileName{};                     ///< Legacy migration key only, runtime-local file IO is disabled.
};

} // namespace yaha
