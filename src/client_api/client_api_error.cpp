#include "client_api/client_api_error.h"

#include <utility>

namespace mqtt {

namespace {

ClientApiErrorCategory classify_client_error_category(
    ClientError source_error) noexcept {
  switch (source_error) {
  case ClientError::ResolveFailed:
  case ClientError::SocketCreateFailed:
  case ClientError::ConnectFailed:
  case ClientError::WriteFailed:
  case ClientError::ReadFailed:
    return ClientApiErrorCategory::Network;
  case ClientError::Timeout:
    return ClientApiErrorCategory::Timeout;
  case ClientError::ProtocolError:
  case ClientError::AliasOutOfRange:
    return ClientApiErrorCategory::Protocol;
  case ClientError::InvalidPacket:
    return ClientApiErrorCategory::Configuration;
  case ClientError::NegotiationRejected:
    return ClientApiErrorCategory::Broker;
  }
  return ClientApiErrorCategory::Unknown;
}

} // namespace

ClientApiException::ClientApiException(ClientApiError error)
    : std::runtime_error(error.message), error_(std::move(error)) {}

const ClientApiError &ClientApiException::error() const noexcept { return error_; }

ClientApiError
client_api_error_from_client_exception(const ClientException &exception) {
  ClientApiError error;
  error.category = classify_client_error_category(exception.error());
  error.message = exception.what();
  error.reason_code = exception.reason_code();
  error.source_error = exception.error();

  if (error.reason_code.has_value()) {
    error.category = classify_reason_code_category(*error.reason_code);
  }

  return error;
}

ClientApiError
client_api_error_from_std_exception(const std::exception &exception) {
  ClientApiError error;
  error.category = ClientApiErrorCategory::Unknown;
  error.message = exception.what();
  return error;
}

ClientApiErrorCategory
classify_reason_code_category(ReasonCode reason_code) noexcept {
  switch (reason_code) {
  case ReasonCode::BadUserNameOrPassword:
  case ReasonCode::BadAuthenticationMethod:
  case ReasonCode::UnsupportedProtocolVersion:
    return ClientApiErrorCategory::Authentication;
  case ReasonCode::NotAuthorized:
  case ReasonCode::Banned:
    return ClientApiErrorCategory::Authorization;
  case ReasonCode::ProtocolError:
  case ReasonCode::MalformedPacket:
  case ReasonCode::ClientIdentifierNotValid:
  case ReasonCode::TopicFilterInvalid:
  case ReasonCode::TopicNameInvalid:
  case ReasonCode::TopicAliasInvalid:
  case ReasonCode::PayloadFormatInvalid:
    return ClientApiErrorCategory::Protocol;
  case ReasonCode::KeepAliveTimeout:
    return ClientApiErrorCategory::Timeout;
  default:
    if (is_error(reason_code)) {
      return ClientApiErrorCategory::Broker;
    }
    return ClientApiErrorCategory::Unknown;
  }
}

ClientApiError client_api_error_from_reason_code(
    ReasonCode reason_code, std::string_view message) {
  ClientApiError error;
  error.category = classify_reason_code_category(reason_code);
  error.message = std::string(message);
  error.reason_code = reason_code;
  return error;
}

} // namespace mqtt
