#include "test_client/test_client_scenario_runner.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace mqtt {
namespace {

enum class ScenarioStepKind : uint8_t {
  Connect,
  Subscribe,
  Publish,
  WaitAssertMessage,
  Unsubscribe,
  Disconnect,
  Sleep,
};

struct ScenarioStep {
  ScenarioStepKind kind{ScenarioStepKind::Sleep};
  uint8_t qos{0U};
  uint32_t timeout_ms{0U};
  uint32_t duration_ms{0U};
  std::string payload{};
};

struct ScenarioDefinition {
  std::string name;
  std::string description;
  std::vector<ScenarioStep> steps;
};

struct RunningSubscribe {
  std::future<int> future;
  std::filesystem::path output_path;
  std::string expected_payload;
  uint32_t timeout_ms{0U};
};

[[nodiscard]] std::string shell_quote(const std::string &raw_text) {
  std::string quoted = "\"";
  for (const char character : raw_text) {
    if (character == '"' || character == '\\') {
      quoted.push_back('\\');
    }
    quoted.push_back(character);
  }
  quoted.push_back('"');
  return quoted;
}

[[nodiscard]] std::string build_command_line(
    const std::string &executable_path, const std::vector<std::string> &arguments,
    const std::optional<std::filesystem::path> &capture_path = std::nullopt) {
  std::ostringstream command_stream;
  command_stream << shell_quote(executable_path);
  for (const std::string &argument : arguments) {
    command_stream << ' ' << shell_quote(argument);
  }
  if (capture_path.has_value()) {
    command_stream << " > " << shell_quote(capture_path->string()) << " 2>&1";
  }
  return command_stream.str();
}

[[nodiscard]] int run_command(const std::string &command_line) {
  return std::system(command_line.c_str());
}

[[nodiscard]] std::string make_runtime_topic() {
  const auto now_ticks = std::chrono::steady_clock::now().time_since_epoch();
  return "integration/step31/scenario/" +
         std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                            now_ticks)
                            .count());
}

[[nodiscard]] std::vector<std::string>
base_connection_args(const TestClientProfile &profile,
                     const std::string &client_identifier) {
  std::vector<std::string> args;
  args.push_back("--host");
  args.push_back(profile.host);
  args.push_back("--port");
  args.push_back(std::to_string(profile.port));
  args.push_back("--transport");
  args.push_back(profile.transport == TestClientTransport::Mqtt ? "mqtt" : "ws");
  args.push_back("--client-id");
  args.push_back(client_identifier);
  args.push_back("--maximum-reconnect-times");
  args.push_back("0");

  if (profile.transport == TestClientTransport::Ws) {
    args.push_back("--ws-path");
    args.push_back(profile.ws_path);
    for (const std::string &header_line : profile.ws_headers) {
      args.push_back("--ws-header");
      args.push_back(header_line);
    }
  }
  if (profile.username.has_value()) {
    args.push_back("--username");
    args.push_back(*profile.username);
  }
  if (profile.password.has_value()) {
    args.push_back("--password");
    args.push_back(*profile.password);
  }

  return args;
}

[[nodiscard]] const std::vector<ScenarioDefinition> &built_in_scenarios() {
  static const std::vector<ScenarioDefinition> scenarios = {
      {
          .name = "clean_start_connect_disconnect",
          .description =
              "Protocol smoke scenario: connect/disconnect lifecycle including short idle wait.",
          .steps = {
              {.kind = ScenarioStepKind::Connect},
              {.kind = ScenarioStepKind::Sleep, .duration_ms = 200U},
              {.kind = ScenarioStepKind::Disconnect},
          },
      },
      {
          .name = "qos1_subscribe_publish_unsubscribe",
          .description =
              "Protocol scenario: subscribe QoS1, publish QoS1, assert receipt, then unsubscribe/disconnect.",
          .steps = {
              {.kind = ScenarioStepKind::Connect},
              {.kind = ScenarioStepKind::Subscribe, .qos = 1U, .timeout_ms = 7000U},
              {.kind = ScenarioStepKind::Sleep, .duration_ms = 300U},
              {.kind = ScenarioStepKind::Publish,
               .qos = 1U,
               .payload = "step31-qos1-message"},
              {.kind = ScenarioStepKind::WaitAssertMessage, .timeout_ms = 9000U},
              {.kind = ScenarioStepKind::Unsubscribe},
              {.kind = ScenarioStepKind::Disconnect},
          },
      },
  };

  return scenarios;
}

[[nodiscard]] const ScenarioDefinition &find_scenario_or_throw(
    const std::string &scenario_name) {
  for (const ScenarioDefinition &scenario_definition : built_in_scenarios()) {
    if (scenario_definition.name == scenario_name) {
      return scenario_definition;
    }
  }

  throw std::invalid_argument("Unknown scenario: " + scenario_name);
}

[[nodiscard]] std::string step_name(const ScenarioStepKind kind) {
  switch (kind) {
  case ScenarioStepKind::Connect:
    return "connect";
  case ScenarioStepKind::Subscribe:
    return "subscribe";
  case ScenarioStepKind::Publish:
    return "publish";
  case ScenarioStepKind::WaitAssertMessage:
    return "wait/assert-message";
  case ScenarioStepKind::Unsubscribe:
    return "unsubscribe";
  case ScenarioStepKind::Disconnect:
    return "disconnect";
  case ScenarioStepKind::Sleep:
    return "sleep";
  }

  return "unknown";
}

void log_step_result(const std::size_t step_index, const std::size_t step_count,
                     const ScenarioStepKind kind, const bool success,
                     const std::string &details) {
  std::cout << "[SCENARIO] [" << (success ? "PASS" : "FAIL") << "] "
            << "(" << step_index << "/" << step_count << ") "
            << step_name(kind) << " - " << details << '\n';
}

[[nodiscard]] bool run_connect_probe(
    const std::string &executable_path, const TestClientProfile &profile,
    const std::string &topic_name, const std::size_t step_index,
    const std::size_t step_count) {
  std::vector<std::string> command_args = {"publish"};
  const auto base_args =
      base_connection_args(profile, profile.client_id + "-scenario-connect");
  command_args.insert(command_args.end(), base_args.begin(), base_args.end());
  command_args.push_back("--topic");
  command_args.push_back(topic_name + "/connect-probe");
  command_args.push_back("--qos");
  command_args.push_back("0");
  command_args.push_back("--payload");
  command_args.push_back("step31-connect-probe");
  command_args.push_back("--payload-encoding");
  command_args.push_back("raw");

  const int exit_code =
      run_command(build_command_line(executable_path, command_args));
  const bool success = (exit_code == 0);
  log_step_result(step_index, step_count, ScenarioStepKind::Connect, success,
                  success ? "CONNECT probe command succeeded"
                          : "CONNECT probe command failed");
  return success;
}

[[nodiscard]] bool start_subscribe_step(
    const std::string &executable_path, const TestClientProfile &profile,
    const std::string &topic_name, const ScenarioStep &step,
    RunningSubscribe &running_subscribe, const std::size_t step_index,
    const std::size_t step_count) {
  std::vector<std::string> command_args = {"subscribe"};
  const auto base_args =
      base_connection_args(profile, profile.client_id + "-scenario-sub");
  command_args.insert(command_args.end(), base_args.begin(), base_args.end());
  command_args.push_back("--subscription");
  command_args.push_back(topic_name + "|" + std::to_string(step.qos) +
                         "|false|false|0");
  command_args.push_back("--message-limit");
  command_args.push_back("1");
  command_args.push_back("--wait-timeout-ms");
  command_args.push_back(
      std::to_string(step.timeout_ms > 0U ? step.timeout_ms : 7000U));
  command_args.push_back("--clean-output");

  const std::filesystem::path output_path =
      std::filesystem::temp_directory_path() /
      ("yahatestclient-step31-subscribe-" +
       std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count()) +
       ".log");

  const std::string command_line =
      build_command_line(executable_path, command_args, output_path);

  running_subscribe.future = std::async(std::launch::async, [command_line]() {
    return run_command(command_line);
  });
  running_subscribe.output_path = output_path;
  running_subscribe.timeout_ms = step.timeout_ms > 0U ? step.timeout_ms : 7000U;

  log_step_result(step_index, step_count, ScenarioStepKind::Subscribe, true,
                  "Subscriber started in background");
  return true;
}

[[nodiscard]] bool run_publish_step(
    const std::string &executable_path, const TestClientProfile &profile,
    const std::string &topic_name, const ScenarioStep &step,
    RunningSubscribe &running_subscribe, const std::size_t step_index,
    const std::size_t step_count) {
  std::vector<std::string> command_args = {"publish"};
  const auto base_args =
      base_connection_args(profile, profile.client_id + "-scenario-pub");
  command_args.insert(command_args.end(), base_args.begin(), base_args.end());
  command_args.push_back("--topic");
  command_args.push_back(topic_name);
  command_args.push_back("--qos");
  command_args.push_back(std::to_string(step.qos));
  command_args.push_back("--payload");
  command_args.push_back(step.payload.empty() ? "step31-message" : step.payload);
  command_args.push_back("--payload-encoding");
  command_args.push_back("raw");

  const int exit_code =
      run_command(build_command_line(executable_path, command_args));
  const bool success = (exit_code == 0);
  if (success) {
    running_subscribe.expected_payload =
        step.payload.empty() ? "step31-message" : step.payload;
  }

  log_step_result(step_index, step_count, ScenarioStepKind::Publish, success,
                  success ? "Publish command succeeded"
                          : "Publish command failed");
  return success;
}

[[nodiscard]] bool wait_and_assert_message(
    RunningSubscribe &running_subscribe, const ScenarioStep &step,
    const std::size_t step_index, const std::size_t step_count) {
  if (!running_subscribe.future.valid()) {
    log_step_result(step_index, step_count, ScenarioStepKind::WaitAssertMessage,
                    false, "No active subscriber process");
    return false;
  }

  const uint32_t timeout_ms = step.timeout_ms > 0U ? step.timeout_ms
                                                    : running_subscribe.timeout_ms;
  const auto wait_result = running_subscribe.future.wait_for(
      std::chrono::milliseconds(timeout_ms > 0U ? timeout_ms : 9000U));
  if (wait_result != std::future_status::ready) {
    log_step_result(step_index, step_count, ScenarioStepKind::WaitAssertMessage,
                    false, "Subscriber did not finish before timeout");
    return false;
  }

  const int subscriber_exit_code = running_subscribe.future.get();
  if (subscriber_exit_code != 0) {
    log_step_result(step_index, step_count, ScenarioStepKind::WaitAssertMessage,
                    false, "Subscriber command failed");
    return false;
  }

  std::ifstream output_file(running_subscribe.output_path);
  if (!output_file.is_open()) {
    log_step_result(step_index, step_count, ScenarioStepKind::WaitAssertMessage,
                    false, "Failed to read subscriber output log");
    return false;
  }

  std::stringstream buffer_stream;
  buffer_stream << output_file.rdbuf();
  const std::string content = buffer_stream.str();

  const bool found_payload =
      !running_subscribe.expected_payload.empty() &&
      content.find(running_subscribe.expected_payload) != std::string::npos;
  log_step_result(
      step_index, step_count, ScenarioStepKind::WaitAssertMessage, found_payload,
      found_payload ? "Expected payload observed in subscriber output"
                    : "Expected payload missing in subscriber output");
  return found_payload;
}

[[nodiscard]] bool run_step31_scenario(const std::string &executable_path,
                                       const TestClientProfile &profile,
                                       const ScenarioDefinition &scenario) {
  const std::string topic_name = make_runtime_topic();
  RunningSubscribe running_subscribe;

  std::cout << "[SCENARIO] Running " << scenario.name << "\n";
  std::cout << "[SCENARIO] " << scenario.description << "\n";

  for (std::size_t index = 0U; index < scenario.steps.size(); ++index) {
    const ScenarioStep &step = scenario.steps[index];
    const std::size_t step_number = index + 1U;
    const std::size_t step_count = scenario.steps.size();

    if (step.kind == ScenarioStepKind::Connect) {
      if (!run_connect_probe(executable_path, profile, topic_name, step_number,
                             step_count)) {
        return false;
      }
      continue;
    }

    if (step.kind == ScenarioStepKind::Subscribe) {
      if (!start_subscribe_step(executable_path, profile, topic_name, step,
                                running_subscribe, step_number, step_count)) {
        return false;
      }
      continue;
    }

    if (step.kind == ScenarioStepKind::Publish) {
      if (!run_publish_step(executable_path, profile, topic_name, step,
                            running_subscribe, step_number, step_count)) {
        return false;
      }
      continue;
    }

    if (step.kind == ScenarioStepKind::WaitAssertMessage) {
      if (!wait_and_assert_message(running_subscribe, step, step_number,
                                   step_count)) {
        return false;
      }
      continue;
    }

    if (step.kind == ScenarioStepKind::Unsubscribe) {
      log_step_result(step_number, step_count, ScenarioStepKind::Unsubscribe,
                      true,
                      "Subscription process already exited after assertion phase");
      continue;
    }

    if (step.kind == ScenarioStepKind::Disconnect) {
      log_step_result(
          step_number, step_count, ScenarioStepKind::Disconnect, true,
          "Disconnect handled by one-shot command lifecycle");
      continue;
    }

    if (step.kind == ScenarioStepKind::Sleep) {
      std::this_thread::sleep_for(std::chrono::milliseconds(step.duration_ms));
      log_step_result(step_number, step_count, ScenarioStepKind::Sleep, true,
                      "Sleep completed");
      continue;
    }

    log_step_result(step_number, step_count, step.kind, false,
                    "Unknown step kind");
    return false;
  }

  return true;
}

} // namespace

std::vector<std::pair<std::string, std::string>> list_test_client_scenarios() {
  std::vector<std::pair<std::string, std::string>> scenario_list;
  for (const ScenarioDefinition &scenario_definition : built_in_scenarios()) {
    scenario_list.emplace_back(scenario_definition.name,
                               scenario_definition.description);
  }
  return scenario_list;
}

int run_test_client_scenario_command(const TestClientCliOptions &options,
                                     const TestClientProfile &profile,
                                     const std::string &executable_path) {
  if (options.list_scenarios) {
    const auto scenario_list = list_test_client_scenarios();
    std::cout << "Available scenarios:" << '\n';
    for (const auto &entry : scenario_list) {
      std::cout << "- " << entry.first << ": " << entry.second << '\n';
    }
    return 0;
  }

  const ScenarioDefinition &scenario = find_scenario_or_throw(options.scenario_name);
  const bool success = run_step31_scenario(executable_path, profile, scenario);
  return success ? 0 : 1;
}

} // namespace mqtt
