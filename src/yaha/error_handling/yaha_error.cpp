#include "yaha/error_handling/yaha_error.h"

#include <format>
#include <utility>

namespace yaha {

YahaError::YahaError(std::string errorCode,
                     std::string message,
                     std::string userMessage,
                     std::optional<std::string> debugDetails)
    : std::runtime_error(composeMessage(errorCode, message, userMessage, debugDetails))
    , errorCode_(std::move(errorCode))
    , message_(std::move(message))
    , userMessage_(std::move(userMessage))
    , debugDetails_(std::move(debugDetails)) {}

const std::string& YahaError::errorCode() const noexcept {
    return errorCode_;
}

const std::string& YahaError::message() const noexcept {
    return message_;
}

const std::string& YahaError::userMessage() const noexcept {
    return userMessage_;
}

const std::optional<std::string>& YahaError::debugDetails() const noexcept {
    return debugDetails_;
}

std::string YahaError::buildMessage() const {
    return composeMessage(errorCode_, message_, userMessage_, debugDetails_);
}

std::string YahaError::composeMessage(const std::string& errorCode,
                                      const std::string& message,
                                      const std::string& userMessage,
                                      const std::optional<std::string>& debugDetails) {
    if (debugDetails.has_value()) {
        return std::format("code={} | message={} | user_message={} | details={}",
                           errorCode,
                           message,
                           userMessage,
                           *debugDetails);
    }

    return std::format("code={} | message={} | user_message={}",
                       errorCode,
                       message,
                       userMessage);
}

} // namespace yaha
