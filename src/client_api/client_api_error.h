#pragma once

/**
 * @file client_api_error.h
 * @brief Unified public client API error model (Step 26).
 */

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "client/client_error.h"

namespace mqtt {

/**
 * @brief High-level error category for public client API operations.
 */
enum class ClientApiErrorCategory : uint8_t {
  Network,
  Protocol,
  Authentication,
  Authorization,
  Broker,
  Timeout,
  Configuration,
  Unknown,
};

/**
 * @brief Structured public client API error payload.
 */
struct ClientApiError {
  ClientApiErrorCategory category{ClientApiErrorCategory::Unknown};
  std::string message;
  std::optional<ReasonCode> reason_code;
  std::optional<ClientError> source_error;
};

/**
 * @brief Exception carrying a structured `ClientApiError` payload.
 */
class ClientApiException : public std::runtime_error {
public:
  /**
   * @brief Construct public API exception from structured error payload.
   * @param error Structured error payload.
   */
  explicit ClientApiException(ClientApiError error);

  /**
   * @brief Return structured error payload.
   */
  [[nodiscard]] const ClientApiError &error() const noexcept;

private:
  ClientApiError error_;
};

/**
 * @brief Map `ClientException` to unified public API error payload.
 * @param exception Internal client exception.
 * @return Structured public error.
 */
[[nodiscard]] ClientApiError
client_api_error_from_client_exception(const ClientException &exception);

/**
 * @brief Map generic std::exception to unified public API error payload.
 * @param exception Generic exception.
 * @return Structured public error.
 */
[[nodiscard]] ClientApiError
client_api_error_from_std_exception(const std::exception &exception);

/**
 * @brief Classify broker reason code to public API error category.
 * @param reason_code MQTT reason code.
 * @return Classified category.
 */
[[nodiscard]] ClientApiErrorCategory
classify_reason_code_category(ReasonCode reason_code) noexcept;

/**
 * @brief Build structured public API error from broker reason code.
 * @param reason_code MQTT reason code.
 * @param message Error message.
 * @return Structured public error.
 */
[[nodiscard]] ClientApiError client_api_error_from_reason_code(
    ReasonCode reason_code, std::string_view message);

} // namespace mqtt
