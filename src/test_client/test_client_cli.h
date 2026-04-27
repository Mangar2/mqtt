#pragma once

/**
 * @file test_client_cli.h
 * @brief Command-line parser for the Step 27 test client shell.
 */

#include <string>
#include <utility>
#include <vector>
#include <cstdint>

namespace mqtt {

/**
 * @brief Supported top-level commands for the test-client executable.
 */
enum class TestClientCommand : uint8_t {
  Help,
  Connect,
  Publish,
  Subscribe,
  SaveProfile,
  ShowProfile,
};

/**
 * @brief Parsed command and arguments.
 */
struct TestClientCliOptions {
  TestClientCommand command{TestClientCommand::Help};
  std::string profile_path{};
  std::string output_path{};
  std::vector<std::pair<std::string, std::string>> overrides{};
};

/**
 * @brief Parse CLI arguments into a normalized command model.
 * @throws std::invalid_argument on malformed arguments.
 */
[[nodiscard]] TestClientCliOptions parse_test_client_cli(int argc,
                                                         const char *argv[]);

/**
 * @brief Return help text for executable usage.
 */
[[nodiscard]] std::string test_client_help_text();

} // namespace mqtt
