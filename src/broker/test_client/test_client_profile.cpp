#include "test_client/test_client_profile.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
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

[[nodiscard]] uint16_t parse_u16(std::string_view text,
                                 std::string_view key_name) {
  const uint32_t parsed_value = parse_uint32(text, key_name);
  if (parsed_value > std::numeric_limits<uint16_t>::max()) {
    throw std::invalid_argument("Numeric value out of range for key '" +
                                std::string(key_name) + "': " +
                                std::string(text));
  }
  return static_cast<uint16_t>(parsed_value);
}

[[nodiscard]] uint8_t parse_u8(std::string_view text,
                               std::string_view key_name) {
  const uint32_t parsed_value = parse_uint32(text, key_name);
  if (parsed_value > std::numeric_limits<uint8_t>::max()) {
    throw std::invalid_argument("Numeric value out of range for key '" +
                                std::string(key_name) + "': " +
                                std::string(text));
  }
  return static_cast<uint8_t>(parsed_value);
}

[[nodiscard]] std::pair<std::string, std::string>
parse_user_property(std::string_view value, std::string_view key_name) {
  const std::size_t equal_index = value.find('=');
  if (equal_index == std::string_view::npos) {
    throw std::invalid_argument("Invalid user property for key '" +
                                std::string(key_name) +
                                "' (expected name=value)");
  }

  const std::string property_name = trim_copy(std::string(value.substr(0U, equal_index)));
  const std::string property_value =
      trim_copy(std::string(value.substr(equal_index + 1U)));
  if (property_name.empty()) {
    throw std::invalid_argument("User property name must not be empty for key '" +
                                std::string(key_name) + "'");
  }
  if (property_value.empty()) {
    throw std::invalid_argument("User property value must not be empty for key '" +
                                std::string(key_name) + "'");
  }

  return std::make_pair(property_name, property_value);
}

[[nodiscard]] bool is_payload_encoding_supported(const std::string_view encoding) {
  return encoding == "raw" || encoding == "json" || encoding == "hex" ||
         encoding == "base64" || encoding == "binary" ||
         encoding == "protobuf" || encoding == "avro";
}

[[nodiscard]] bool
is_correlation_encoding_supported(const std::string_view encoding) {
  return encoding == "raw" || encoding == "hex" || encoding == "base64";
}

void validate_user_properties_or_throw(
    const std::vector<std::pair<std::string, std::string>> &properties,
    const std::string_view key_name) {
  for (const auto &property_entry : properties) {
    if (property_entry.first.empty() || property_entry.second.empty()) {
      throw std::invalid_argument("Profile " + std::string(key_name) +
                                  " entries must use non-empty name/value");
    }
  }
}

[[nodiscard]] bool has_any_will_fields(const TestClientProfile &profile) {
  return !profile.will_payload.empty() || profile.will_qos != 0U ||
         profile.will_retain || profile.will_delay_interval_seconds != 0U ||
         profile.will_payload_format_indicator.has_value() ||
         profile.will_message_expiry_interval_seconds.has_value() ||
         profile.will_content_type.has_value() ||
         profile.will_response_topic.has_value() ||
         profile.will_correlation_data.has_value() ||
         !profile.will_user_properties.empty();
}

void validate_subscribe_entry_or_throw(const std::string &entry) {
  std::size_t offset = 0U;
  const auto next_field = [&entry, &offset]() -> std::optional<std::string_view> {
    if (offset > entry.size()) {
      return std::nullopt;
    }
    const std::size_t separator_index = entry.find('|', offset);
    if (separator_index == std::string::npos) {
      const std::string_view tail(entry.data() + offset, entry.size() - offset);
      offset = entry.size() + 1U;
      return tail;
    }

    const std::string_view field(entry.data() + offset,
                                 separator_index - offset);
    offset = separator_index + 1U;
    return field;
  };

  const auto topic_filter = next_field();
  const auto qos_field = next_field();
  const auto no_local_field = next_field();
  const auto retain_as_published_field = next_field();
  const auto retain_handling_field = next_field();
  const auto extra_field = next_field();

  if (!topic_filter.has_value() || !qos_field.has_value() ||
      !no_local_field.has_value() || !retain_as_published_field.has_value() ||
      !retain_handling_field.has_value() || extra_field.has_value()) {
    throw std::invalid_argument(
        "Profile subscribe_entry must use format filter|qos|no_local|retain_as_published|retain_handling");
  }

  if (trim_copy(std::string(*topic_filter)).empty()) {
    throw std::invalid_argument("Profile subscribe_entry requires non-empty filter");
  }

  const uint8_t qos_value = parse_u8(*qos_field, "subscribe_entry.qos");
  if (qos_value > 2U) {
    throw std::invalid_argument("Profile subscribe_entry.qos must be in range 0..2");
  }

  (void)parse_bool(*no_local_field, "subscribe_entry.no_local");
  (void)parse_bool(*retain_as_published_field,
                   "subscribe_entry.retain_as_published");

  const uint8_t retain_handling_value =
      parse_u8(*retain_handling_field, "subscribe_entry.retain_handling");
  if (retain_handling_value > 2U) {
    throw std::invalid_argument(
        "Profile subscribe_entry.retain_handling must be in range 0..2");
  }
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
  if (profile.maximum_packet_size > 0U &&
      profile.maximum_packet_size < 16U) {
    throw std::invalid_argument(
        "Profile maximum_packet_size must be zero or at least 16");
  }
  if (profile.receive_maximum == 0U) {
    throw std::invalid_argument("Profile receive_maximum must be greater than zero");
  }

  validate_user_properties_or_throw(profile.connect_user_properties,
                                    "connect_user_properties");

  if (profile.authentication_method.has_value() &&
      profile.authentication_method->empty()) {
    throw std::invalid_argument(
        "Profile authentication_method must not be empty when set");
  }
  if (profile.authentication_data.has_value() &&
      !profile.authentication_method.has_value()) {
    throw std::invalid_argument(
        "Profile authentication_data requires authentication_method");
  }

  if (profile.will_topic.has_value()) {
    if (profile.will_topic->empty()) {
      throw std::invalid_argument("Profile will_topic must not be empty when set");
    }
    if (profile.will_qos > 2U) {
      throw std::invalid_argument("Profile will_qos must be in range 0..2");
    }
  } else if (has_any_will_fields(profile)) {
    throw std::invalid_argument(
        "Profile will_* options require will_topic to be set");
  }

  if (profile.will_payload_format_indicator.has_value() &&
      *profile.will_payload_format_indicator > 1U) {
    throw std::invalid_argument(
        "Profile will_payload_format_indicator must be 0 or 1");
  }
  if (profile.will_content_type.has_value() && profile.will_content_type->empty()) {
    throw std::invalid_argument(
        "Profile will_content_type must not be empty when set");
  }
  if (profile.will_response_topic.has_value() &&
      profile.will_response_topic->empty()) {
    throw std::invalid_argument(
        "Profile will_response_topic must not be empty when set");
  }
  validate_user_properties_or_throw(profile.will_user_properties,
                                    "will_user_properties");

  if (profile.publish_topic.has_value() && profile.publish_topic->empty()) {
    throw std::invalid_argument(
        "Profile publish_topic must not be empty when set");
  }
  if (profile.publish_qos > 2U) {
    throw std::invalid_argument("Profile publish_qos must be in range 0..2");
  }
  if (profile.publish_dup && profile.publish_qos == 0U) {
    throw std::invalid_argument("Profile publish_dup requires publish_qos > 0");
  }

  const uint8_t payload_source_count =
      static_cast<uint8_t>(profile.publish_payload.has_value() ? 1U : 0U) +
      static_cast<uint8_t>(profile.publish_payload_stdin ? 1U : 0U) +
      static_cast<uint8_t>(profile.publish_payload_stdin_multiline ? 1U : 0U) +
      static_cast<uint8_t>(profile.publish_payload_file.has_value() ? 1U : 0U) +
      static_cast<uint8_t>(profile.publish_payload_size > 0U ? 1U : 0U);
  if (payload_source_count > 1U) {
    throw std::invalid_argument(
        "Profile publish payload source is ambiguous; choose exactly one of publish_payload, publish_payload_stdin, publish_payload_stdin_multiline, publish_payload_file, publish_payload_size");
  }

  if (profile.publish_protobuf_path.has_value() &&
      profile.publish_protobuf_path->empty()) {
    throw std::invalid_argument(
        "Profile publish_protobuf_path must not be empty when set");
  }
  if (profile.publish_protobuf_message_name.has_value() &&
      profile.publish_protobuf_message_name->empty()) {
    throw std::invalid_argument(
        "Profile publish_protobuf_message_name must not be empty when set");
  }
  if (profile.publish_avsc_path.has_value() &&
      profile.publish_avsc_path->empty()) {
    throw std::invalid_argument(
        "Profile publish_avsc_path must not be empty when set");
  }

  if (!is_payload_encoding_supported(profile.publish_payload_encoding)) {
    throw std::invalid_argument(
        "Profile publish_payload_encoding is unsupported");
  }
  if (profile.publish_payload_encoding == "protobuf") {
    if (!profile.publish_protobuf_path.has_value()) {
      throw std::invalid_argument(
          "Profile publish_payload_encoding protobuf requires publish_protobuf_path");
    }
    if (!profile.publish_protobuf_message_name.has_value()) {
      throw std::invalid_argument(
          "Profile publish_payload_encoding protobuf requires publish_protobuf_message_name");
    }
  }
  if (profile.publish_payload_encoding == "avro" &&
      !profile.publish_avsc_path.has_value()) {
    throw std::invalid_argument(
        "Profile publish_payload_encoding avro requires publish_avsc_path");
  }
  if (!is_correlation_encoding_supported(
          profile.publish_correlation_data_encoding)) {
    throw std::invalid_argument(
        "Profile publish_correlation_data_encoding is unsupported");
  }

  if (profile.publish_payload_format_indicator.has_value() &&
      *profile.publish_payload_format_indicator > 1U) {
    throw std::invalid_argument(
        "Profile publish_payload_format_indicator must be 0 or 1");
  }
  if (profile.publish_topic_alias.has_value() &&
      *profile.publish_topic_alias == 0U) {
    throw std::invalid_argument("Profile publish_topic_alias must be > 0");
  }
  if (profile.publish_response_topic.has_value() &&
      profile.publish_response_topic->empty()) {
    throw std::invalid_argument(
        "Profile publish_response_topic must not be empty when set");
  }
  if (profile.publish_content_type.has_value() &&
      profile.publish_content_type->empty()) {
    throw std::invalid_argument(
        "Profile publish_content_type must not be empty when set");
  }
  if (profile.publish_subscription_identifier.has_value() &&
      *profile.publish_subscription_identifier == 0U) {
    throw std::invalid_argument(
        "Profile publish_subscription_identifier must be > 0");
  }

  validate_user_properties_or_throw(profile.publish_user_properties,
                                    "publish_user_properties");

  for (const std::string &subscribe_entry : profile.subscribe_entries) {
    validate_subscribe_entry_or_throw(subscribe_entry);
  }
  if (profile.subscribe_identifier.has_value() &&
      *profile.subscribe_identifier == 0U) {
    throw std::invalid_argument("Profile subscribe_identifier must be > 0");
  }
  validate_user_properties_or_throw(profile.subscribe_user_properties,
                                    "subscribe_user_properties");
  if (!is_payload_encoding_supported(profile.subscribe_payload_format)) {
    throw std::invalid_argument("Profile subscribe_payload_format is unsupported");
  }
  if (profile.subscribe_payload_format == "protobuf") {
    if (!profile.subscribe_protobuf_path.has_value() ||
        profile.subscribe_protobuf_path->empty()) {
      throw std::invalid_argument(
          "Profile subscribe_payload_format protobuf requires subscribe_protobuf_path");
    }
    if (!profile.subscribe_protobuf_message_name.has_value() ||
        profile.subscribe_protobuf_message_name->empty()) {
      throw std::invalid_argument(
          "Profile subscribe_payload_format protobuf requires subscribe_protobuf_message_name");
    }
  }
  if (profile.subscribe_payload_format == "avro" &&
      (!profile.subscribe_avsc_path.has_value() ||
       profile.subscribe_avsc_path->empty())) {
    throw std::invalid_argument(
        "Profile subscribe_payload_format avro requires subscribe_avsc_path");
  }
  if (profile.subscribe_output_append &&
      !profile.subscribe_output_file.has_value()) {
    throw std::invalid_argument(
        "Profile subscribe_output_append requires subscribe_output_file");
  }
  if (profile.subscribe_output_file.has_value() &&
      profile.subscribe_output_file->empty()) {
    throw std::invalid_argument(
        "Profile subscribe_output_file must not be empty when set");
  }
  if (profile.subscribe_output_file_save.has_value() &&
      profile.subscribe_output_file_save->empty()) {
    throw std::invalid_argument(
        "Profile subscribe_output_file_save must not be empty when set");
  }
  if (profile.subscribe_output_delimiter.empty()) {
    throw std::invalid_argument(
        "Profile subscribe_output_delimiter must not be empty");
  }
  if (profile.subscribe_output_format.has_value() &&
      profile.subscribe_output_format->empty()) {
    throw std::invalid_argument(
        "Profile subscribe_output_format must not be empty when set");
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
  if (key == "session_expiry_interval_seconds") {
    profile.session_expiry_interval_seconds = parse_uint32(value, key);
    return;
  }
  if (key == "receive_maximum") {
    profile.receive_maximum = parse_u16(value, key);
    return;
  }
  if (key == "maximum_packet_size") {
    profile.maximum_packet_size = parse_uint32(value, key);
    return;
  }
  if (key == "topic_alias_maximum") {
    profile.topic_alias_maximum = parse_u16(value, key);
    return;
  }
  if (key == "request_response_information") {
    profile.request_response_information = parse_bool(value, key);
    return;
  }
  if (key == "request_problem_information") {
    profile.request_problem_information = parse_bool(value, key);
    return;
  }
  if (key == "connect_user_property") {
    profile.connect_user_properties.push_back(
        parse_user_property(value, key));
    return;
  }
  if (key == "authentication_method") {
    profile.authentication_method = std::string(value);
    return;
  }
  if (key == "authentication_data") {
    profile.authentication_data = std::string(value);
    return;
  }
  if (key == "will_topic") {
    profile.will_topic = std::string(value);
    return;
  }
  if (key == "will_payload") {
    profile.will_payload = std::string(value);
    return;
  }
  if (key == "will_qos") {
    profile.will_qos = parse_u8(value, key);
    return;
  }
  if (key == "will_retain") {
    profile.will_retain = parse_bool(value, key);
    return;
  }
  if (key == "will_delay_interval_seconds") {
    profile.will_delay_interval_seconds = parse_uint32(value, key);
    return;
  }
  if (key == "will_payload_format_indicator") {
    profile.will_payload_format_indicator = parse_u8(value, key);
    return;
  }
  if (key == "will_message_expiry_interval_seconds") {
    profile.will_message_expiry_interval_seconds = parse_uint32(value, key);
    return;
  }
  if (key == "will_content_type") {
    profile.will_content_type = std::string(value);
    return;
  }
  if (key == "will_response_topic") {
    profile.will_response_topic = std::string(value);
    return;
  }
  if (key == "will_correlation_data") {
    profile.will_correlation_data = std::string(value);
    return;
  }
  if (key == "will_user_property") {
    profile.will_user_properties.push_back(parse_user_property(value, key));
    return;
  }
  if (key == "publish_topic") {
    profile.publish_topic = std::string(value);
    return;
  }
  if (key == "publish_qos") {
    profile.publish_qos = parse_u8(value, key);
    return;
  }
  if (key == "publish_retain") {
    profile.publish_retain = parse_bool(value, key);
    return;
  }
  if (key == "publish_dup") {
    profile.publish_dup = parse_bool(value, key);
    return;
  }
  if (key == "publish_payload") {
    profile.publish_payload = std::string(value);
    return;
  }
  if (key == "publish_payload_stdin") {
    profile.publish_payload_stdin = parse_bool(value, key);
    return;
  }
  if (key == "publish_payload_stdin_multiline") {
    profile.publish_payload_stdin_multiline = parse_bool(value, key);
    return;
  }
  if (key == "publish_payload_file") {
    profile.publish_payload_file = std::string(value);
    return;
  }
  if (key == "publish_protobuf_path") {
    profile.publish_protobuf_path = std::string(value);
    return;
  }
  if (key == "publish_protobuf_message_name") {
    profile.publish_protobuf_message_name = std::string(value);
    return;
  }
  if (key == "publish_avsc_path") {
    profile.publish_avsc_path = std::string(value);
    return;
  }
  if (key == "publish_payload_size") {
    profile.publish_payload_size = parse_uint32(value, key);
    return;
  }
  if (key == "publish_payload_encoding") {
    profile.publish_payload_encoding = std::string(value);
    return;
  }
  if (key == "publish_payload_format_indicator") {
    profile.publish_payload_format_indicator = parse_u8(value, key);
    return;
  }
  if (key == "publish_message_expiry_interval_seconds") {
    profile.publish_message_expiry_interval_seconds = parse_uint32(value, key);
    return;
  }
  if (key == "publish_topic_alias") {
    profile.publish_topic_alias = parse_u16(value, key);
    return;
  }
  if (key == "publish_response_topic") {
    profile.publish_response_topic = std::string(value);
    return;
  }
  if (key == "publish_correlation_data") {
    profile.publish_correlation_data = std::string(value);
    return;
  }
  if (key == "publish_correlation_data_encoding") {
    profile.publish_correlation_data_encoding = std::string(value);
    return;
  }
  if (key == "publish_subscription_identifier") {
    profile.publish_subscription_identifier = parse_uint32(value, key);
    return;
  }
  if (key == "publish_content_type") {
    profile.publish_content_type = std::string(value);
    return;
  }
  if (key == "publish_user_property") {
    profile.publish_user_properties.push_back(parse_user_property(value, key));
    return;
  }
  if (key == "subscribe_entry") {
    profile.subscribe_entries.push_back(std::string(value));
    return;
  }
  if (key == "subscribe_identifier") {
    profile.subscribe_identifier = parse_uint32(value, key);
    return;
  }
  if (key == "subscribe_user_property") {
    profile.subscribe_user_properties.push_back(parse_user_property(value, key));
    return;
  }
  if (key == "subscribe_payload_format") {
    profile.subscribe_payload_format = std::string(value);
    return;
  }
  if (key == "subscribe_protobuf_path") {
    profile.subscribe_protobuf_path = std::string(value);
    return;
  }
  if (key == "subscribe_protobuf_message_name") {
    profile.subscribe_protobuf_message_name = std::string(value);
    return;
  }
  if (key == "subscribe_avsc_path") {
    profile.subscribe_avsc_path = std::string(value);
    return;
  }
  if (key == "subscribe_clean_output") {
    profile.subscribe_clean_output = parse_bool(value, key);
    return;
  }
  if (key == "subscribe_verbose_packets") {
    profile.subscribe_verbose_packets = parse_bool(value, key);
    return;
  }
  if (key == "subscribe_output_file") {
    profile.subscribe_output_file = std::string(value);
    return;
  }
  if (key == "subscribe_output_file_save") {
    profile.subscribe_output_file_save = std::string(value);
    return;
  }
  if (key == "subscribe_output_append") {
    profile.subscribe_output_append = parse_bool(value, key);
    return;
  }
  if (key == "subscribe_output_delimiter") {
    profile.subscribe_output_delimiter = std::string(value);
    return;
  }
  if (key == "subscribe_output_format") {
    profile.subscribe_output_format = std::string(value);
    return;
  }
  if (key == "subscribe_message_limit") {
    profile.subscribe_message_limit = parse_uint32(value, key);
    return;
  }
  if (key == "subscribe_wait_timeout_ms") {
    profile.subscribe_wait_timeout_ms = parse_uint32(value, key);
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
  loaded_profile.connect_user_properties.clear();
  loaded_profile.will_user_properties.clear();
  loaded_profile.publish_user_properties.clear();
  loaded_profile.subscribe_entries.clear();
  loaded_profile.subscribe_user_properties.clear();

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

  output_stream << "session_expiry_interval_seconds="
                << profile.session_expiry_interval_seconds << '\n';
  output_stream << "receive_maximum=" << profile.receive_maximum << '\n';
  output_stream << "maximum_packet_size=" << profile.maximum_packet_size
                << '\n';
  output_stream << "topic_alias_maximum=" << profile.topic_alias_maximum
                << '\n';
  output_stream << "request_response_information="
                << (profile.request_response_information ? "true" : "false")
                << '\n';
  output_stream << "request_problem_information="
                << (profile.request_problem_information ? "true" : "false")
                << '\n';
  for (const auto &entry : profile.connect_user_properties) {
    output_stream << "connect_user_property=" << entry.first << '=' << entry.second
                  << '\n';
  }
  if (profile.authentication_method.has_value()) {
    output_stream << "authentication_method=" << *profile.authentication_method
                  << '\n';
  }
  if (profile.authentication_data.has_value()) {
    output_stream << "authentication_data=" << *profile.authentication_data
                  << '\n';
  }

  if (profile.will_topic.has_value()) {
    output_stream << "will_topic=" << *profile.will_topic << '\n';
  }
  output_stream << "will_payload=" << profile.will_payload << '\n';
  output_stream << "will_qos=" << static_cast<uint32_t>(profile.will_qos)
                << '\n';
  output_stream << "will_retain=" << (profile.will_retain ? "true" : "false")
                << '\n';
  output_stream << "will_delay_interval_seconds="
                << profile.will_delay_interval_seconds << '\n';
  if (profile.will_payload_format_indicator.has_value()) {
    output_stream << "will_payload_format_indicator="
                  << static_cast<uint32_t>(*profile.will_payload_format_indicator)
                  << '\n';
  }
  if (profile.will_message_expiry_interval_seconds.has_value()) {
    output_stream << "will_message_expiry_interval_seconds="
                  << *profile.will_message_expiry_interval_seconds << '\n';
  }
  if (profile.will_content_type.has_value()) {
    output_stream << "will_content_type=" << *profile.will_content_type << '\n';
  }
  if (profile.will_response_topic.has_value()) {
    output_stream << "will_response_topic=" << *profile.will_response_topic
                  << '\n';
  }
  if (profile.will_correlation_data.has_value()) {
    output_stream << "will_correlation_data=" << *profile.will_correlation_data
                  << '\n';
  }
  for (const auto &entry : profile.will_user_properties) {
    output_stream << "will_user_property=" << entry.first << '=' << entry.second
                  << '\n';
  }

  if (profile.publish_topic.has_value()) {
    output_stream << "publish_topic=" << *profile.publish_topic << '\n';
  }
  output_stream << "publish_qos=" << static_cast<uint32_t>(profile.publish_qos)
                << '\n';
  output_stream << "publish_retain="
                << (profile.publish_retain ? "true" : "false") << '\n';
  output_stream << "publish_dup=" << (profile.publish_dup ? "true" : "false")
                << '\n';
  if (profile.publish_payload.has_value()) {
    output_stream << "publish_payload=" << *profile.publish_payload << '\n';
  }
  output_stream << "publish_payload_stdin="
                << (profile.publish_payload_stdin ? "true" : "false") << '\n';
  output_stream << "publish_payload_stdin_multiline="
                << (profile.publish_payload_stdin_multiline ? "true" : "false")
                << '\n';
  if (profile.publish_payload_file.has_value()) {
    output_stream << "publish_payload_file=" << *profile.publish_payload_file
                  << '\n';
  }
  if (profile.publish_protobuf_path.has_value()) {
    output_stream << "publish_protobuf_path=" << *profile.publish_protobuf_path
                  << '\n';
  }
  if (profile.publish_protobuf_message_name.has_value()) {
    output_stream << "publish_protobuf_message_name="
                  << *profile.publish_protobuf_message_name << '\n';
  }
  if (profile.publish_avsc_path.has_value()) {
    output_stream << "publish_avsc_path=" << *profile.publish_avsc_path
                  << '\n';
  }
  output_stream << "publish_payload_size=" << profile.publish_payload_size
                << '\n';
  output_stream << "publish_payload_encoding=" << profile.publish_payload_encoding
                << '\n';
  if (profile.publish_payload_format_indicator.has_value()) {
    output_stream << "publish_payload_format_indicator="
                  << static_cast<uint32_t>(*profile.publish_payload_format_indicator)
                  << '\n';
  }
  if (profile.publish_message_expiry_interval_seconds.has_value()) {
    output_stream << "publish_message_expiry_interval_seconds="
                  << *profile.publish_message_expiry_interval_seconds << '\n';
  }
  if (profile.publish_topic_alias.has_value()) {
    output_stream << "publish_topic_alias=" << *profile.publish_topic_alias
                  << '\n';
  }
  if (profile.publish_response_topic.has_value()) {
    output_stream << "publish_response_topic=" << *profile.publish_response_topic
                  << '\n';
  }
  if (profile.publish_correlation_data.has_value()) {
    output_stream << "publish_correlation_data="
                  << *profile.publish_correlation_data << '\n';
  }
  output_stream << "publish_correlation_data_encoding="
                << profile.publish_correlation_data_encoding << '\n';
  if (profile.publish_subscription_identifier.has_value()) {
    output_stream << "publish_subscription_identifier="
                  << *profile.publish_subscription_identifier << '\n';
  }
  if (profile.publish_content_type.has_value()) {
    output_stream << "publish_content_type=" << *profile.publish_content_type
                  << '\n';
  }
  for (const auto &entry : profile.publish_user_properties) {
    output_stream << "publish_user_property=" << entry.first << '='
                  << entry.second << '\n';
  }

  for (const std::string &entry : profile.subscribe_entries) {
    output_stream << "subscribe_entry=" << entry << '\n';
  }
  if (profile.subscribe_identifier.has_value()) {
    output_stream << "subscribe_identifier=" << *profile.subscribe_identifier
                  << '\n';
  }
  for (const auto &entry : profile.subscribe_user_properties) {
    output_stream << "subscribe_user_property=" << entry.first << '='
                  << entry.second << '\n';
  }
  output_stream << "subscribe_payload_format=" << profile.subscribe_payload_format
                << '\n';
  if (profile.subscribe_protobuf_path.has_value()) {
    output_stream << "subscribe_protobuf_path="
                  << *profile.subscribe_protobuf_path << '\n';
  }
  if (profile.subscribe_protobuf_message_name.has_value()) {
    output_stream << "subscribe_protobuf_message_name="
                  << *profile.subscribe_protobuf_message_name << '\n';
  }
  if (profile.subscribe_avsc_path.has_value()) {
    output_stream << "subscribe_avsc_path=" << *profile.subscribe_avsc_path
                  << '\n';
  }
  output_stream << "subscribe_clean_output="
                << (profile.subscribe_clean_output ? "true" : "false")
                << '\n';
  output_stream << "subscribe_verbose_packets="
                << (profile.subscribe_verbose_packets ? "true" : "false")
                << '\n';
  if (profile.subscribe_output_file.has_value()) {
    output_stream << "subscribe_output_file=" << *profile.subscribe_output_file
                  << '\n';
  }
  if (profile.subscribe_output_file_save.has_value()) {
    output_stream << "subscribe_output_file_save="
                  << *profile.subscribe_output_file_save << '\n';
  }
  output_stream << "subscribe_output_append="
                << (profile.subscribe_output_append ? "true" : "false")
                << '\n';
  output_stream << "subscribe_output_delimiter="
                << profile.subscribe_output_delimiter << '\n';
  if (profile.subscribe_output_format.has_value()) {
    output_stream << "subscribe_output_format="
                  << *profile.subscribe_output_format << '\n';
  }
  output_stream << "subscribe_message_limit=" << profile.subscribe_message_limit
                << '\n';
  output_stream << "subscribe_wait_timeout_ms="
                << profile.subscribe_wait_timeout_ms << '\n';

  if (!output_stream.good()) {
    throw std::runtime_error("Failed to write complete profile file: " + path);
  }
}

} // namespace mqtt
