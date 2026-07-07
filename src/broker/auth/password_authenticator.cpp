#include "auth/password_authenticator.h"

#include "data_model/property/property_id.h"
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

[[nodiscard]] bool
has_auth_method(const std::vector<mqtt::Property> &properties,
                std::string_view expected_method) {
  for (const mqtt::Property &property : properties) {
    if (property.id != mqtt::PropertyId::AuthenticationMethod) {
      continue;
    }
    const auto &method_name = std::get<mqtt::Utf8String>(property.value).value;
    return method_name == expected_method;
  }
  return false;
}

[[nodiscard]] std::optional<mqtt::BinaryData>
find_auth_data(const std::vector<mqtt::Property> &properties) {
  for (const mqtt::Property &property : properties) {
    if (property.id == mqtt::PropertyId::AuthenticationData) {
      return std::get<mqtt::BinaryData>(property.value);
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::pair<std::string, mqtt::BinaryData>>
decode_plain_payload(const mqtt::BinaryData &payload) {
  std::string text_payload;
  text_payload.reserve(payload.data.size());
  for (uint8_t byte_value : payload.data) {
    text_payload.push_back(static_cast<char>(byte_value));
  }

  const std::size_t separator_pos = text_payload.find(':');
  if (separator_pos == std::string::npos || separator_pos == 0U ||
      separator_pos + 1U >= text_payload.size()) {
    return std::nullopt;
  }

  const std::string user_name = text_payload.substr(0U, separator_pos);
  const std::string pass_text = text_payload.substr(separator_pos + 1U);

  mqtt::BinaryData pass_binary;
  pass_binary.data.reserve(pass_text.size());
  for (char chr : pass_text) {
    pass_binary.data.push_back(static_cast<uint8_t>(chr));
  }

  return std::make_pair(user_name, pass_binary);
}

[[nodiscard]] mqtt::AuthResult bad_credentials_result() {
  return {.status = mqtt::AuthStatus::Failure,
          .reason_code = mqtt::ReasonCode::BadUserNameOrPassword,
          .auth_data = {}};
}

[[nodiscard]] mqtt::AuthResult bad_method_result() {
  return {.status = mqtt::AuthStatus::Failure,
          .reason_code = mqtt::ReasonCode::BadAuthenticationMethod,
          .auth_data = {}};
}

[[nodiscard]] mqtt::AuthResult continue_result() {
  mqtt::BinaryData server_prompt;
  server_prompt.data = {'c', 'r', 'e', 'd', 's'};
  return {.status = mqtt::AuthStatus::Continue,
          .reason_code = mqtt::ReasonCode::ContinueAuthentication,
          .auth_data = server_prompt};
}

} // namespace

namespace mqtt {

void PasswordAuthenticator::add_credential(const Utf8String &username,
                                           const BinaryData &password) {
  credentials_[username.value] = password;
}

void PasswordAuthenticator::remove_credential(const Utf8String &username) {
  credentials_.erase(username.value);
}

AuthResult PasswordAuthenticator::authenticate(const ConnectPacket &connect) {
  auto validate_credentials =
      [this](std::string_view user_name,
             const BinaryData &pass_word) -> AuthResult {
    const auto found = credentials_.find(std::string(user_name));
    if (found == credentials_.end() || found->second != pass_word) {
      return bad_credentials_result();
    }

    return {.status = AuthStatus::Success,
            .reason_code = ReasonCode::Success,
            .auth_data = {}};
  };

  if (!connect.properties.empty()) {
    if (!has_auth_method(connect.properties, "PLAIN")) {
      return bad_method_result();
    }

    const std::optional<BinaryData> connect_auth_data =
        find_auth_data(connect.properties);
    if (connect_auth_data.has_value()) {
      const auto decoded = decode_plain_payload(*connect_auth_data);
      if (!decoded.has_value()) {
        return bad_credentials_result();
      }
      return validate_credentials(decoded->first, decoded->second);
    }

    if (!connect.username.has_value() || !connect.password.has_value()) {
      return continue_result();
    }
  }

  if (!connect.username.has_value()) {
    return bad_credentials_result();
  }

  if (!connect.password.has_value()) {
    return bad_credentials_result();
  }

  return validate_credentials(connect.username->value,
                              connect.password.value());
}

AuthResult PasswordAuthenticator::on_auth(const AuthPacket &auth_pkt) {
  if (!has_auth_method(auth_pkt.properties, "PLAIN")) {
    return bad_method_result();
  }

  const std::optional<BinaryData> auth_data =
      find_auth_data(auth_pkt.properties);
  if (!auth_data.has_value()) {
    return continue_result();
  }

  const auto decoded = decode_plain_payload(*auth_data);
  if (!decoded.has_value()) {
    return bad_credentials_result();
  }

  const std::string &user_name = decoded->first;
  const BinaryData &pass_word = decoded->second;

  const auto found = credentials_.find(user_name);
  if (found == credentials_.end() || found->second != pass_word) {
    return bad_credentials_result();
  }

  return {.status = AuthStatus::Success,
          .reason_code = ReasonCode::Success,
          .auth_data = {}};
}

} // namespace mqtt
