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

TEST_CASE("test_client_scenario_command_step32_mass_connect_mode_returns_failure_without_broker",
          "[test_client][scenario]") {
  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.load_mode = "mass-connect";
  options.load_connection_count = 3U;
  options.load_connect_interval_ms = 0U;
  options.load_publish_limit = 3U;
  options.load_topic_template = "step32/{index}";
  options.load_client_template = "step32-client-{index}";

    TestClientProfile profile;
    profile.host = "127.0.0.1";
    profile.port = 1U;

    CHECK(run_test_client_scenario_command(options, profile, "ignored") == 1);
}

TEST_CASE("test_client_scenario_command_rejects_unknown_step32_mode",
          "[test_client][scenario]") {
  const std::filesystem::path script_path =
      make_temp_script_path("scenario_step32_invalid_mode", true);

  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.load_mode = "unsupported-mode";

  const TestClientProfile profile;
  CHECK(run_test_client_scenario_command(options, profile, script_path.string()) ==
        1);
}

TEST_CASE("test_client_scenario_command_step32_publish_rate_mode_returns_failure_without_broker",
          "[test_client][scenario]") {
  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.load_mode = "publish-rate";
  options.load_connection_count = 1U;
  options.load_publish_limit = 2U;
  options.load_message_interval_ms = 0U;
  options.load_topic_template = "step32-rate/{index}";
  options.load_client_template = "step32-rate-client-{index}";

    TestClientProfile profile;
    profile.host = "127.0.0.1";
    profile.port = 1U;

    CHECK(run_test_client_scenario_command(options, profile, "ignored") == 1);
}

TEST_CASE("test_client_scenario_command_step32_multi_subscribe_mode_returns_failure_without_broker",
          "[test_client][scenario]") {
  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.load_mode = "multi-subscribe";
  options.load_connection_count = 1U;
  options.load_publish_limit = 1U;
  options.load_topic_template = "step32-sub/{index}";
  options.load_client_template = "step32-sub-client-{index}";

    TestClientProfile profile;
    profile.host = "127.0.0.1";
    profile.port = 1U;

    CHECK(run_test_client_scenario_command(options, profile, "ignored") == 1);
}

} // namespace mqtt
