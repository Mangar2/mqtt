#include "test_client/test_client_profile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mqtt {
namespace {

[[nodiscard]] std::string trim_copy(std::string text) {
  const auto first_it = std::find_if_not(text.begin(), text.end(),
                                         [](unsigned char value) {
                                           return std::isspace(value) != 0;
                                         });
  if (first_it == text.end()) {
    return {};
  }

  const auto last_it = std::find_if_not(text.rbegin(), text.rend(),
                                        [](unsigned char value) {
                                          return std::isspace(value) != 0;
                                        })
                           .base();
  return std::string(first_it, last_it);
}

[[nodiscard]] bool parse_bool(std::string_view text, std::string_view key) {
  if (text == "true" || text == "1" || text == "yes") {
    return true;
  }
  if (text == "false" || text == "0" || text == "no") {
    return false;
  }

  throw std::invalid_argument("Invalid boolean for key '" + std::string(key) +
                              "': " + std::string(text));
}

[[nodiscard]] uint32_t parse_uint32(std::string_view text,
                                    std::string_view key_name) {
  if (text.empty()) {
    throw std::invalid_argument("Missing numeric value for key '" +
                                std::string(key_name) + "'");
  }

  uint64_t parsed_value = 0U;
  for (const char character : text) {
    if (character < '0' || character > '9') {
      throw std::invalid_argument("Invalid numeric value for key '" +
                                  std::string(key_name) + "': " +
                                  std::string(text));
    }
    parsed_value = parsed_value * 10U + static_cast<uint64_t>(character - '0');
    if (parsed_value > static_cast<uint64_t>(UINT32_MAX)) {
      throw std::invalid_argument("Numeric value out of range for key '" +
                                  std::string(key_name) + "': " +
                                  std::string(text));
    }
  }

  return static_cast<uint32_t>(parsed_value);
}

} // namespace

std::string to_string(const TestClientTransport transport) {
  if (transport == TestClientTransport::Mqtt) {
    return "mqtt";
  }
  return "ws";
}

std::optional<TestClientTransport>
transport_from_string(const std::string_view text) noexcept {
  if (text == "mqtt") {
    return TestClientTransport::Mqtt;
  }
  if (text == "ws") {
    return TestClientTransport::Ws;
  }
  return std::nullopt;
}

void validate_test_client_profile_or_throw(const TestClientProfile &profile) {
  if (profile.host.empty()) {
    throw std::invalid_argument("Profile host must not be empty");
  }
  if (profile.port == 0U) {
    throw std::invalid_argument("Profile port must be greater than zero");
  }
  if (profile.client_id.empty()) {
    throw std::invalid_argument("Profile client_id must not be empty");
  }
  if (profile.keep_alive_seconds == 0U) {
    throw std::invalid_argument(
        "Profile keep_alive_seconds must be greater than zero");
  }
  if (profile.reconnect_period_ms == 0U) {
    throw std::invalid_argument(
        "Profile reconnect_period_ms must be greater than zero");
  }

  if (profile.transport == TestClientTransport::Ws) {
    if (profile.ws_path.empty()) {
      throw std::invalid_argument("Profile ws_path must not be empty for ws");
    }
    if (profile.ws_path.front() != '/') {
      throw std::invalid_argument(
          "Profile ws_path must start with '/' for ws transport");
    }
  }

  if (profile.username.has_value() && profile.username->empty()) {
    throw std::invalid_argument("Profile username must not be empty when set");
  }
  if (profile.password.has_value() && profile.password->empty()) {
    throw std::invalid_argument("Profile password must not be empty when set");
  }
}

void apply_profile_override(TestClientProfile &profile, const std::string_view key,
                            const std::string_view value) {
  if (key == "host") {
    profile.host = std::string(value);
    return;
  }
  if (key == "port") {
    const uint32_t port_number = parse_uint32(value, key);
    if (port_number == 0U || port_number > 65535U) {
      throw std::invalid_argument("Port out of range: " + std::string(value));
    }
    profile.port = static_cast<uint16_t>(port_number);
    return;
  }
  if (key == "transport") {
    const auto parsed_transport = transport_from_string(value);
    if (!parsed_transport.has_value()) {
      throw std::invalid_argument("Unsupported transport: " +
                                  std::string(value));
    }
    profile.transport = *parsed_transport;
    return;
  }
  if (key == "ws_path") {
    profile.ws_path = std::string(value);
    return;
  }
  if (key == "ws_header") {
    profile.ws_headers.push_back(std::string(value));
    return;
  }
  if (key == "client_id") {
    profile.client_id = std::string(value);
    return;
  }
  if (key == "clean_start") {
    profile.clean_start = parse_bool(value, key);
    return;
  }
  if (key == "keep_alive_seconds") {
    const uint32_t keep_alive_seconds = parse_uint32(value, key);
    if (keep_alive_seconds == 0U || keep_alive_seconds > 65535U) {
      throw std::invalid_argument("keep_alive_seconds out of range: " +
                                  std::string(value));
    }
    profile.keep_alive_seconds = static_cast<uint16_t>(keep_alive_seconds);
    return;
  }
  if (key == "username") {
    profile.username = std::string(value);
    return;
  }
  if (key == "password") {
    profile.password = std::string(value);
    return;
  }
  if (key == "reconnect_period_ms") {
    profile.reconnect_period_ms = parse_uint32(value, key);
    return;
  }
  if (key == "maximum_reconnect_times") {
    profile.maximum_reconnect_times = parse_uint32(value, key);
    return;
  }

  throw std::invalid_argument("Unknown profile key: " + std::string(key));
}

TestClientProfile load_test_client_profile_from_file(const std::string &path) {
  std::ifstream input_stream(path);
  if (!input_stream.is_open()) {
    throw std::runtime_error("Failed to open profile file for reading: " +
                             path);
  }

  TestClientProfile loaded_profile;
  loaded_profile.ws_headers.clear();

  std::string line;
  uint32_t line_number = 0U;
  while (std::getline(input_stream, line)) {
    ++line_number;
    const std::string trimmed_line = trim_copy(line);
    if (trimmed_line.empty() || trimmed_line.front() == '#') {
      continue;
    }

    const std::size_t equal_sign_index = trimmed_line.find('=');
    if (equal_sign_index == std::string::npos) {
      throw std::invalid_argument("Invalid profile line without '=' at line " +
                                  std::to_string(line_number));
    }

    const std::string key_name =
        trim_copy(trimmed_line.substr(0U, equal_sign_index));
    const std::string value =
        trim_copy(trimmed_line.substr(equal_sign_index + 1U));
    if (key_name.empty()) {
      throw std::invalid_argument("Invalid empty key at line " +
                                  std::to_string(line_number));
    }

    apply_profile_override(loaded_profile, key_name, value);
  }

  validate_test_client_profile_or_throw(loaded_profile);
  return loaded_profile;
}

void save_test_client_profile_to_file(const std::string &path,
                                      const TestClientProfile &profile) {
  validate_test_client_profile_or_throw(profile);

  std::ofstream output_stream(path, std::ios::trunc);
  if (!output_stream.is_open()) {
    throw std::runtime_error("Failed to open profile file for writing: " +
                             path);
  }

  output_stream << "# yaha test client profile\n";
  output_stream << "host=" << profile.host << '\n';
  output_stream << "port=" << profile.port << '\n';
  output_stream << "transport=" << to_string(profile.transport) << '\n';
  output_stream << "ws_path=" << profile.ws_path << '\n';

  for (const std::string &header_value : profile.ws_headers) {
    output_stream << "ws_header=" << header_value << '\n';
  }

  output_stream << "client_id=" << profile.client_id << '\n';
  output_stream << "clean_start=" << (profile.clean_start ? "true" : "false")
                << '\n';
  output_stream << "keep_alive_seconds=" << profile.keep_alive_seconds << '\n';

  if (profile.username.has_value()) {
    output_stream << "username=" << *profile.username << '\n';
  }
  if (profile.password.has_value()) {
    output_stream << "password=" << *profile.password << '\n';
  }

  output_stream << "reconnect_period_ms=" << profile.reconnect_period_ms
                << '\n';
  output_stream << "maximum_reconnect_times=" << profile.maximum_reconnect_times
                << '\n';

  if (!output_stream.good()) {
    throw std::runtime_error("Failed to write complete profile file: " + path);
  }
}

} // namespace mqtt
