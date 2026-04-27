#pragma once

/**
 * @file test_client_profile.h
 * @brief Persistent connection-profile model for the Step 27 test client shell.
 */

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mqtt {

/**
 * @brief Broker-supported transport variants for the test client shell.
 */
enum class TestClientTransport : uint8_t {
  Mqtt,
  Ws,
};

/**
 * @brief Persistent profile data used by the Step 27 test client shell.
 */
struct TestClientProfile {
  std::string host{"127.0.0.1"};
  uint16_t port{1883U};
  TestClientTransport transport{TestClientTransport::Mqtt};

  std::string ws_path{"/mqtt"};
  std::vector<std::string> ws_headers{};

  std::string client_id{"yaha-test-client"};
  bool clean_start{true};
  uint16_t keep_alive_seconds{30U};

  std::optional<std::string> username;
  std::optional<std::string> password;

  uint32_t session_expiry_interval_seconds{0U};
  uint16_t receive_maximum{65535U};
  uint32_t maximum_packet_size{0U};
  uint16_t topic_alias_maximum{0U};
  bool request_response_information{false};
  bool request_problem_information{true};
  std::vector<std::pair<std::string, std::string>> connect_user_properties{};
  std::optional<std::string> authentication_method;
  std::optional<std::string> authentication_data;

  std::optional<std::string> will_topic;
  std::string will_payload{};
  uint8_t will_qos{0U};
  bool will_retain{false};
  uint32_t will_delay_interval_seconds{0U};
  std::optional<uint8_t> will_payload_format_indicator;
  std::optional<uint32_t> will_message_expiry_interval_seconds;
  std::optional<std::string> will_content_type;
  std::optional<std::string> will_response_topic;
  std::optional<std::string> will_correlation_data;
  std::vector<std::pair<std::string, std::string>> will_user_properties{};

  std::optional<std::string> publish_topic;
  uint8_t publish_qos{0U};
  bool publish_retain{false};
  bool publish_dup{false};
  std::optional<std::string> publish_payload;
  bool publish_payload_stdin{false};
  bool publish_payload_stdin_multiline{false};
  std::optional<std::string> publish_payload_file;
  std::string publish_payload_encoding{"raw"};
  std::optional<uint8_t> publish_payload_format_indicator;
  std::optional<uint32_t> publish_message_expiry_interval_seconds;
  std::optional<uint16_t> publish_topic_alias;
  std::optional<std::string> publish_response_topic;
  std::optional<std::string> publish_correlation_data;
  std::string publish_correlation_data_encoding{"raw"};
  std::optional<uint32_t> publish_subscription_identifier;
  std::optional<std::string> publish_content_type;
  std::vector<std::pair<std::string, std::string>> publish_user_properties{};

  uint32_t reconnect_period_ms{1000U};
  uint32_t maximum_reconnect_times{10U};
};

/**
 * @brief Return canonical text representation for one transport value.
 */
[[nodiscard]] std::string to_string(TestClientTransport transport);

/**
 * @brief Parse text into transport enum.
 * @return Parsed value or empty optional for invalid input.
 */
[[nodiscard]] std::optional<TestClientTransport>
transport_from_string(std::string_view text) noexcept;

/**
 * @brief Validate profile fields and throw on invalid values.
 * @throws std::invalid_argument when any value is invalid.
 */
void validate_test_client_profile_or_throw(const TestClientProfile &profile);

/**
 * @brief Apply one key/value override to the profile.
 *
 * Supported keys:
 * - host
 * - port
 * - transport
 * - ws_path
 * - ws_header (repeatable)
 * - client_id
 * - clean_start
 * - keep_alive_seconds
 * - username
 * - password
 * - session_expiry_interval_seconds
 * - receive_maximum
 * - maximum_packet_size
 * - topic_alias_maximum
 * - request_response_information
 * - request_problem_information
 * - connect_user_property (repeatable key=value)
 * - authentication_method
 * - authentication_data
 * - will_topic
 * - will_payload
 * - will_qos
 * - will_retain
 * - will_delay_interval_seconds
 * - will_payload_format_indicator
 * - will_message_expiry_interval_seconds
 * - will_content_type
 * - will_response_topic
 * - will_correlation_data
 * - will_user_property (repeatable key=value)
 * - publish_topic
 * - publish_qos
 * - publish_retain
 * - publish_dup
 * - publish_payload
 * - publish_payload_stdin
 * - publish_payload_stdin_multiline
 * - publish_payload_file
 * - publish_payload_encoding
 * - publish_payload_format_indicator
 * - publish_message_expiry_interval_seconds
 * - publish_topic_alias
 * - publish_response_topic
 * - publish_correlation_data
 * - publish_correlation_data_encoding
 * - publish_subscription_identifier
 * - publish_content_type
 * - publish_user_property (repeatable key=value)
 * - reconnect_period_ms
 * - maximum_reconnect_times
 *
 * @throws std::invalid_argument for unknown keys or invalid values.
 */
void apply_profile_override(TestClientProfile &profile, std::string_view key,
                            std::string_view value);

/**
 * @brief Load profile from a key=value file.
 * @throws std::runtime_error or std::invalid_argument on I/O or parse errors.
 */
[[nodiscard]] TestClientProfile
load_test_client_profile_from_file(const std::string &path);

/**
 * @brief Save profile as deterministic key=value file.
 * @throws std::runtime_error on write errors.
 */
void save_test_client_profile_to_file(const std::string &path,
                                      const TestClientProfile &profile);

} // namespace mqtt
