#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "test_client/test_client_scenario_runner.h"

namespace mqtt {
namespace {

constexpr int k_exit_success{0};
constexpr int k_exit_failure{1};

std::filesystem::path make_temp_script_path(const std::string& base_name) {
  const std::filesystem::path directory =
      std::filesystem::path("test") / "tmp" / "test_client_scenario_coverage";
  std::error_code error_code;
  std::filesystem::create_directories(directory, error_code);

#if defined(_WIN32)
  const std::filesystem::path script_path = directory / (base_name + ".bat");
  std::ofstream script_file(script_path, std::ios::trunc);
  REQUIRE(script_file.is_open());
  script_file << "@echo off\n";
  script_file << "echo scenario-coverage\n";
  script_file << "exit /b 0\n";
#else
  const std::filesystem::path script_path = directory / base_name;
  std::ofstream script_file(script_path, std::ios::trunc);
  REQUIRE(script_file.is_open());
  script_file << "#!/bin/sh\n";
  script_file << "echo scenario-coverage\n";
  script_file << "exit 0\n";
  std::filesystem::permissions(
      script_path,
      std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
          std::filesystem::perms::others_exec,
      std::filesystem::perm_options::add,
      error_code);
#endif

  return script_path;
}

TEST_CASE("test_client_scenario_command_step31_uses_auth_and_connect_properties", "[test_client][scenario]") {
  const std::filesystem::path script_path = make_temp_script_path("scenario_cov_success");

  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.scenario_name = "clean_start_connect_disconnect";

  TestClientProfile profile;
  profile.client_id = "scenario-auth-client";
  profile.username = std::string{"user-a"};
  profile.password = std::string{"pass-a"};
  profile.connect_user_properties.emplace_back("k1", "v1");
  profile.connect_user_properties.emplace_back("k2", "v2");

  CHECK(run_test_client_scenario_command(options, profile, script_path.string()) ==
        k_exit_success);
}

TEST_CASE("test_client_scenario_command_step31_reports_spawn_failure", "[test_client][scenario]") {
  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.scenario_name = "qos1_subscribe_publish_unsubscribe";

  TestClientProfile profile;
  profile.client_id = "scenario-missing-binary-client";

  CHECK(run_test_client_scenario_command(options, profile, "/path/does/not/exist") ==
        k_exit_failure);
}

} // namespace
} // namespace mqtt
