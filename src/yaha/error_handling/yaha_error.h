#pragma once

/**
 * @file yaha_error.h
 * @brief Shared YAHA error type for throw and HTTP error reporting.
 */

#include <optional>
#include <stdexcept>
#include <string>

namespace yaha {

/**
 * @brief Unified YAHA error model for throw paths and API error responses.
 */
class YahaError final : public std::runtime_error {
public:
    /**
     * @brief Builds one YahaError with machine code and user-facing messages.
     * @param errorCode Stable machine-readable error code.
     * @param message Technical error message.
     * @param userMessage User-friendly message.
     * @param debugDetails Optional debugging details.
     */
    YahaError(std::string errorCode,
              std::string message,
              std::string userMessage,
              std::optional<std::string> debugDetails = std::nullopt);

    /**
     * @brief Returns stable machine-readable error code.
     * @return Error code.
     */
    [[nodiscard]] const std::string& errorCode() const noexcept;

    /**
     * @brief Returns technical error message.
     * @return Technical message.
     */
    [[nodiscard]] const std::string& message() const noexcept;

    /**
     * @brief Returns user-friendly error message.
     * @return User message.
     */
    [[nodiscard]] const std::string& userMessage() const noexcept;

    /**
     * @brief Returns optional debug details.
     * @return Debug details when present.
     */
    [[nodiscard]] const std::optional<std::string>& debugDetails() const noexcept;

    /**
     * @brief Builds full formatted error message for output channels.
     * @return Combined output string.
     */
    [[nodiscard]] std::string buildMessage() const;

private:
    static std::string composeMessage(const std::string& errorCode,
                                      const std::string& message,
                                      const std::string& userMessage,
                                      const std::optional<std::string>& debugDetails);

    std::string errorCode_{};
    std::string message_{};
    std::string userMessage_{};
    std::optional<std::string> debugDetails_{};
};

} // namespace yaha
