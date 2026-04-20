#pragma once

/**
 * @file connect_result.h
 * @brief Result type for broker CONNECT handling workflows.
 */

#include <optional>
#include <string>
#include <vector>

#include "auth/authenticator.h"
#include "data_model/property/property.h"
#include "data_model/reason_code/reason_code.h"
#include "data_model/types/binary_data.h"

namespace mqtt {

/**
 * @brief Full outcome of Broker CONNECT handling.
 */
struct ConnectResult {
  AuthStatus auth_status{
      AuthStatus::Success};    ///< Authentication stage outcome.
  bool session_present{false}; ///< CONNACK Session Present flag.
  ReasonCode reason_code{ReasonCode::Success}; ///< Final connection outcome.
  std::optional<BinaryData>
      auth_data;           ///< AUTH payload when `auth_status == Continue`.
  std::string auth_method; ///< Negotiated auth method for enhanced auth.
  std::vector<Property> connack_properties; ///< CONNACK properties from broker
                                            ///< configuration.
  std::string client_id; ///< Final client identifier used for this connection.
};

} // namespace mqtt
