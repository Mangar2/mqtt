#pragma once

/**
 * @file test_client_scenario_runner.h
 * @brief Built-in scenario catalog and scripted scenario runner for yahatestclient.
 */

#include <string>
#include <utility>
#include <vector>

#include "test_client/test_client_cli.h"
#include "test_client/test_client_profile.h"

namespace mqtt {

/**
 * @brief Return built-in scenario names and short descriptions.
 */
[[nodiscard]] std::vector<std::pair<std::string, std::string>>
list_test_client_scenarios();

/**
 * @brief Execute Step 31 scenario command or Step 32 load mode.
 * @param options Parsed CLI options.
 * @param profile Effective profile after merge and validation.
 * @param executable_path Executable path used to spawn sub-commands.
 * @return Process exit code (`0` success, non-zero failure).
 */
[[nodiscard]] int run_test_client_scenario_command(
    const TestClientCliOptions &options, const TestClientProfile &profile,
    const std::string &executable_path);

} // namespace mqtt
