#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_client/test_client_scenario_runner.h"

namespace mqtt {
namespace {

std::filesystem::path make_temp_script_path(const std::string &base_name,
                                            const bool success_script) {
  const std::filesystem::path directory =
      std::filesystem::path("test") / "tmp" / "test_client_scenario";
  std::error_code error_code;
  std::filesystem::create_directories(directory, error_code);

#if defined(_WIN32)
  const std::filesystem::path script_path = directory / (base_name + ".bat");
  std::ofstream script_file(script_path, std::ios::trunc);
  REQUIRE(script_file.is_open());
  script_file << "@echo off\n";
  script_file << "echo step31-qos1-message\n";
  script_file << (success_script ? "exit /b 0\n" : "exit /b 1\n");
#else
  const std::filesystem::path script_path = directory / base_name;
  std::ofstream script_file(script_path, std::ios::trunc);
  REQUIRE(script_file.is_open());
  script_file << "#!/bin/sh\n";
  script_file << "echo step31-qos1-message\n";
  script_file << (success_script ? "exit 0\n" : "exit 1\n");
  std::filesystem::permissions(
      script_path,
      std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
          std::filesystem::perms::others_exec,
      std::filesystem::perm_options::add, error_code);
#endif

  return script_path;
}

} // namespace

TEST_CASE("test_client_scenario_catalog_lists_step31_builtins",
          "[test_client][scenario]") {
  const std::vector<std::pair<std::string, std::string>> scenario_list =
      list_test_client_scenarios();

  REQUIRE(scenario_list.size() >= 2U);
  CHECK(scenario_list[0].first == "clean_start_connect_disconnect");
  CHECK(scenario_list[1].first == "qos1_subscribe_publish_unsubscribe");
}

TEST_CASE("test_client_scenario_command_list_mode_succeeds",
          "[test_client][scenario]") {
  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.list_scenarios = true;

  const TestClientProfile profile;
  CHECK(run_test_client_scenario_command(options, profile, "ignored") == 0);
}

TEST_CASE("test_client_scenario_command_unknown_name_fails_fast",
          "[test_client][scenario]") {
  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.scenario_name = "missing-scenario";

  const TestClientProfile profile;
  CHECK_THROWS_AS(run_test_client_scenario_command(options, profile, "ignored"),
                  std::invalid_argument);
}

TEST_CASE("test_client_scenario_command_executes_qos1_scenario_successfully",
          "[test_client][scenario]") {
  const std::filesystem::path script_path =
      make_temp_script_path("scenario_success", true);

  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.scenario_name = "qos1_subscribe_publish_unsubscribe";

  const TestClientProfile profile;
  CHECK(run_test_client_scenario_command(options, profile, script_path.string()) ==
        0);
}

TEST_CASE("test_client_scenario_command_propagates_step_failures",
          "[test_client][scenario]") {
  const std::filesystem::path script_path =
      make_temp_script_path("scenario_failure", false);

  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.scenario_name = "clean_start_connect_disconnect";

  const TestClientProfile profile;
  CHECK(run_test_client_scenario_command(options, profile, script_path.string()) ==
        1);
}

} // namespace mqtt
