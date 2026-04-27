#pragma once

/**
 * @file test_client_profile.h
 * @brief Persistent connection-profile model for the Step 27 test client shell.
 */

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
