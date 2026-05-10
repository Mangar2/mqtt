#pragma once

/**
 * @file remote_service_config.h
 * @brief RemoteService runtime configuration contract.
 */

#include "yaha/message/message.h"

#include <cstdint>
#include <string>

namespace yaha {

inline constexpr std::uint16_t kDefaultRemoteServiceListenPort = 9123U;
inline constexpr std::uint16_t kDefaultRemoteServiceFileStorePort = 8210U;

/**
 * @brief Runtime configuration for RemoteService component behavior.
 */
struct RemoteServiceConfig {
    std::string listenHost{"0.0.0.0"}; ///< HTTP listener host.
    std::uint16_t listenPort{kDefaultRemoteServiceListenPort}; ///< HTTP listener port.
    Qos subscribeQos{Qos::AtLeastOnce}; ///< QoS for monitor subscription and command publish default.
    std::string monitorTopicPrefix{"$MONITOR/FileStore"}; ///< FileStore monitor topic prefix.
    std::string fileStoreHost{"127.0.0.1"}; ///< FileStore HTTP host.
    std::uint16_t fileStorePort{kDefaultRemoteServiceFileStorePort}; ///< FileStore HTTP port.
    std::string mappingKeyPath{}; ///< Required FileStore key path for service mapping payload.
};

} // namespace yaha