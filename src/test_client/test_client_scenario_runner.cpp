#include "test_client_scenario_runner.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace mqtt {
namespace {

using Clock = std::chrono::steady_clock;

struct ScenarioStep {
  std::string label;
  std::vector<std::string> arguments;
};

struct BuiltinScenario {
  std::string name;
  std::string description;
  std::vector<ScenarioStep> steps;
};

struct LoadMetrics {
  std::string mode;
  uint64_t attempted{0U};
  uint64_t succeeded{0U};
  uint64_t failed{0U};
  uint64_t timed_out{0U};
  uint64_t duration_ms{0U};
  double throughput_ops_per_sec{0.0};
  double latency_avg_ms{0.0};
  double latency_min_ms{0.0};
  double latency_max_ms{0.0};
  std::vector<double> sample_latencies_ms{};
};

struct SubscriberTask {
  std::future<int> task;
  std::string topic;
  std::string output_file;
};

std::string shell_quote(const std::string& value) {
  std::string quoted;
  quoted.reserve(value.size() + 2U);
  quoted.push_back('\'');
  for (const char character : value) {
    if (character == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(character);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

std::string build_cmdline(const std::string& executable,
                         const std::vector<std::string>& arguments) {
  std::ostringstream command_stream;
  command_stream << shell_quote(executable);
  for (const auto& argument : arguments) {
    command_stream << " " << shell_quote(argument);
  }
  return command_stream.str();
}

int run_command(const std::string& command_line) {
  const int raw_exit = std::system(command_line.c_str());
  if (raw_exit < 0) {
    return 1;
  }
#if defined(_WIN32)
  return raw_exit;
#else
  if (WIFEXITED(raw_exit)) {
    return WEXITSTATUS(raw_exit);
  }
  return 1;
#endif
}

std::vector<std::string> make_base_arguments(
    const TestClientProfile& profile,
    const std::string& generated_client_id = std::string()) {
  std::vector<std::string> arguments;
  arguments.emplace_back("--host");
  arguments.emplace_back(profile.host);
  arguments.emplace_back("--port");
  arguments.emplace_back(std::to_string(profile.port));
  arguments.emplace_back("--transport");
  arguments.emplace_back(to_string(profile.transport));

  if (!generated_client_id.empty()) {
    arguments.emplace_back("--client-id");
    arguments.emplace_back(generated_client_id);
  } else if (!profile.client_id.empty()) {
    arguments.emplace_back("--client-id");
    arguments.emplace_back(profile.client_id);
  }

  if (profile.username.has_value()) {
    arguments.emplace_back("--username");
    arguments.emplace_back(*profile.username);
  }
  if (profile.password.has_value()) {
    arguments.emplace_back("--password");
    arguments.emplace_back(*profile.password);
  }

  arguments.emplace_back("--clean-start");
  arguments.emplace_back(profile.clean_start ? "true" : "false");
  arguments.emplace_back("--keep-alive");
  arguments.emplace_back(std::to_string(profile.keep_alive_seconds));
  arguments.emplace_back("--session-expiry-seconds");
  arguments.emplace_back(
      std::to_string(profile.session_expiry_interval_seconds));

  for (const auto& property : profile.connect_user_properties) {
    arguments.emplace_back("--connect-user-property");
    arguments.emplace_back(property.first + "=" + property.second);
  }

  return arguments;
}

std::string render_template(const std::string& pattern, uint32_t index) {
  const std::string placeholder = "{index}";
  const std::string replacement = std::to_string(index);
  std::string rendered = pattern;

  std::size_t start_index = 0U;
  while (true) {
    const std::size_t found = rendered.find(placeholder, start_index);
    if (found == std::string::npos) {
      break;
    }
    rendered.replace(found, placeholder.size(), replacement);
    start_index = found + replacement.size();
  }

  return rendered;
}

void sleep_between_operations(uint32_t interval_ms) {
  if (interval_ms == 0U) {
    return;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
}

std::string make_payload_for_index(uint32_t index,
                                   const TestClientProfile& profile) {
  if (profile.publish_payload.has_value()) {
    return *profile.publish_payload;
  }
  return "step32-message-" + std::to_string(index);
}

std::vector<std::string> make_publish_arguments(
    const TestClientProfile& profile,
    uint32_t index,
    const std::string& topic,
    const std::string& client_id) {
  std::vector<std::string> arguments;
  arguments.emplace_back("publish");

  const std::vector<std::string> base = make_base_arguments(profile, client_id);
  arguments.insert(arguments.end(), base.begin(), base.end());

  arguments.emplace_back("--topic");
  arguments.emplace_back(topic);
  arguments.emplace_back("--payload");
  arguments.emplace_back(make_payload_for_index(index, profile));
  arguments.emplace_back("--payload-encoding");
  arguments.emplace_back(profile.publish_payload_encoding);
  arguments.emplace_back("--qos");
  arguments.emplace_back(std::to_string(profile.publish_qos));
  arguments.emplace_back("--retain");
  arguments.emplace_back(profile.publish_retain ? "true" : "false");

  if (profile.publish_payload_format_indicator.has_value()) {
    arguments.emplace_back("--payload-format-indicator");
    arguments.emplace_back(
        std::to_string(*profile.publish_payload_format_indicator));
  }
  if (profile.publish_message_expiry_interval_seconds.has_value()) {
    arguments.emplace_back("--message-expiry-interval-seconds");
    arguments.emplace_back(
        std::to_string(*profile.publish_message_expiry_interval_seconds));
  }
  if (profile.publish_topic_alias.has_value()) {
    arguments.emplace_back("--topic-alias");
    arguments.emplace_back(std::to_string(*profile.publish_topic_alias));
  }
  if (profile.publish_response_topic.has_value()) {
    arguments.emplace_back("--response-topic");
    arguments.emplace_back(*profile.publish_response_topic);
  }
  if (profile.publish_correlation_data.has_value()) {
    arguments.emplace_back("--correlation-data");
    arguments.emplace_back(*profile.publish_correlation_data);
  }
  if (profile.publish_content_type.has_value()) {
    arguments.emplace_back("--content-type");
    arguments.emplace_back(*profile.publish_content_type);
  }

  for (const auto& property : profile.publish_user_properties) {
    arguments.emplace_back("--publish-user-property");
    arguments.emplace_back(property.first + "=" + property.second);
  }

  return arguments;
}

std::vector<std::string> make_subscribe_arguments(
    const TestClientProfile& profile,
    const std::string& topic,
    const std::string& client_id,
    uint32_t message_limit,
    const std::string& output_file) {
  std::vector<std::string> arguments;
  arguments.emplace_back("subscribe");

  const std::vector<std::string> base = make_base_arguments(profile, client_id);
  arguments.insert(arguments.end(), base.begin(), base.end());

  arguments.emplace_back("--subscription");
  arguments.emplace_back(topic + "|0|false|false|0");
  arguments.emplace_back("--message-limit");
  arguments.emplace_back(std::to_string(message_limit));
  arguments.emplace_back("--wait-timeout-ms");
  arguments.emplace_back("15000");
  arguments.emplace_back("--clean-output");
  arguments.emplace_back("--output-file");
  arguments.emplace_back(output_file);

  return arguments;
}

void update_latency_stats(LoadMetrics& metrics) {
  if (metrics.sample_latencies_ms.empty()) {
    return;
  }

  const auto min_max_pair = std::minmax_element(metrics.sample_latencies_ms.begin(),
                                                metrics.sample_latencies_ms.end());
  metrics.latency_min_ms = *min_max_pair.first;
  metrics.latency_max_ms = *min_max_pair.second;

  double total_sum = 0.0;
  for (const double value : metrics.sample_latencies_ms) {
    total_sum += value;
  }
  metrics.latency_avg_ms =
      total_sum / static_cast<double>(metrics.sample_latencies_ms.size());
}

void finalize_metrics(LoadMetrics& metrics, Clock::time_point start_time) {
  metrics.duration_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          Clock::now() - start_time)
          .count());

  const double duration_seconds = static_cast<double>(metrics.duration_ms) / 1000.0;
  if (duration_seconds > 0.0) {
    metrics.throughput_ops_per_sec =
        static_cast<double>(metrics.succeeded) / duration_seconds;
  }

  update_latency_stats(metrics);
}

void print_metrics_summary(const LoadMetrics& metrics, bool print_json) {
  std::cout << "Step32 load mode: " << metrics.mode << "\n"
            << "  attempted=" << metrics.attempted << " succeeded="
            << metrics.succeeded << " failed=" << metrics.failed
            << " timed_out=" << metrics.timed_out << "\n"
            << "  duration_ms=" << metrics.duration_ms
            << " throughput_ops_per_sec=" << std::fixed << std::setprecision(2)
            << metrics.throughput_ops_per_sec << "\n"
            << "  latency_ms(avg/min/max)=" << std::fixed
            << std::setprecision(2) << metrics.latency_avg_ms << "/"
            << metrics.latency_min_ms << "/" << metrics.latency_max_ms << "\n";

  if (!print_json) {
    return;
  }

  std::ostringstream json_stream;
  json_stream << "{"
              << "\"mode\":\"" << metrics.mode << "\","
              << "\"attempted\":" << metrics.attempted << ","
              << "\"succeeded\":" << metrics.succeeded << ","
              << "\"failed\":" << metrics.failed << ","
              << "\"timed_out\":" << metrics.timed_out << ","
              << "\"duration_ms\":" << metrics.duration_ms << ","
              << "\"throughput_ops_per_sec\":" << std::fixed
              << std::setprecision(3) << metrics.throughput_ops_per_sec << ","
              << "\"latency_avg_ms\":" << std::fixed << std::setprecision(3)
              << metrics.latency_avg_ms << ","
              << "\"latency_min_ms\":" << std::fixed << std::setprecision(3)
              << metrics.latency_min_ms << ","
              << "\"latency_max_ms\":" << std::fixed << std::setprecision(3)
              << metrics.latency_max_ms << "}";

  std::cout << "LOAD_METRICS_JSON " << json_stream.str() << "\n";
}

void run_single_operation(
    const std::string& executable,
    const std::vector<std::string>& arguments,
    LoadMetrics& metrics) {
  const Clock::time_point started_at = Clock::now();
  const int result_code = run_command(build_cmdline(executable, arguments));
  const Clock::time_point finished_at = Clock::now();

  metrics.attempted += 1U;
  metrics.sample_latencies_ms.push_back(
      static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                              finished_at - started_at)
                              .count()));

  if (result_code == 0) {
    metrics.succeeded += 1U;
  } else {
    metrics.failed += 1U;
  }
}

int run_mass_connect_mode(
    const std::string& executable,
    const TestClientCliOptions& options,
    const TestClientProfile& profile,
    LoadMetrics& metrics) {
  for (uint32_t connection_index = 0U;
       connection_index < options.load_connection_count; ++connection_index) {
    const std::string client_id =
        render_template(options.load_client_template, connection_index);
    const std::string topic =
        render_template(options.load_topic_template, connection_index);

    const std::vector<std::string> arguments =
        make_publish_arguments(profile, connection_index, topic, client_id);
    run_single_operation(executable, arguments, metrics);

    sleep_between_operations(options.load_connect_interval_ms);
  }

  return metrics.failed == 0U ? 0 : 1;
}

int run_publish_rate_mode(
    const std::string& executable,
    const TestClientCliOptions& options,
    const TestClientProfile& profile,
    LoadMetrics& metrics) {
  const uint32_t operation_count =
      std::max(options.load_publish_limit, options.load_connection_count);

  for (uint32_t publish_index = 0U; publish_index < operation_count;
       ++publish_index) {
    const uint32_t connection_index =
        publish_index % std::max(options.load_connection_count, 1U);
    const std::string client_id =
        render_template(options.load_client_template, connection_index);
    const std::string topic =
        render_template(options.load_topic_template, connection_index);

    const std::vector<std::string> arguments =
        make_publish_arguments(profile, publish_index, topic, client_id);
    run_single_operation(executable, arguments, metrics);

    sleep_between_operations(options.load_message_interval_ms);
  }

  return metrics.failed == 0U ? 0 : 1;
}

int run_multi_subscribe_mode(
    const std::string& executable,
    const TestClientCliOptions& options,
    const TestClientProfile& profile,
    LoadMetrics& metrics) {
  const std::filesystem::path output_directory =
      std::filesystem::temp_directory_path() / "yaha-step32-subscribers";
  std::error_code error_code;
  std::filesystem::create_directories(output_directory, error_code);

  std::vector<SubscriberTask> subscribers;
  subscribers.reserve(options.load_connection_count);

  for (uint32_t subscriber_index = 0U;
       subscriber_index < options.load_connection_count; ++subscriber_index) {
    const std::string topic =
        render_template(options.load_topic_template, subscriber_index);
    const std::string client_id =
        render_template(options.load_client_template, subscriber_index);
    const std::filesystem::path output_file =
        output_directory / ("subscriber-" + std::to_string(subscriber_index) + ".log");

    const std::vector<std::string> subscribe_arguments = make_subscribe_arguments(
        profile,
        topic,
        client_id,
        1U,
        output_file.string());

    SubscriberTask task{
        std::async(
            std::launch::async,
            [executable, subscribe_arguments]() {
              return run_command(build_cmdline(executable, subscribe_arguments));
            }),
        topic,
        output_file.string()};
    subscribers.push_back(std::move(task));

    sleep_between_operations(options.load_connect_interval_ms);
  }

  const uint32_t publish_count =
      std::max(options.load_publish_limit, options.load_connection_count);

  for (uint32_t publish_index = 0U; publish_index < publish_count;
       ++publish_index) {
    const uint32_t subscriber_index =
        publish_index % std::max(options.load_connection_count, 1U);
    const std::string topic =
        render_template(options.load_topic_template, subscriber_index);
    const std::string client_id =
        render_template(options.load_client_template, subscriber_index);

    const std::vector<std::string> publish_arguments =
        make_publish_arguments(profile, publish_index, topic, client_id);
    run_single_operation(executable, publish_arguments, metrics);

    sleep_between_operations(options.load_message_interval_ms);
  }

  for (auto& subscriber : subscribers) {
    metrics.attempted += 1U;
    const Clock::time_point started_at = Clock::now();
    const int result_code = subscriber.task.get();
    const Clock::time_point finished_at = Clock::now();

    metrics.sample_latencies_ms.push_back(
        static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                finished_at - started_at)
                                .count()));

    if (result_code == 0) {
      metrics.succeeded += 1U;
    } else {
      metrics.failed += 1U;
      std::cout << "Subscriber failed for topic " << subscriber.topic
                << " output=" << subscriber.output_file << "\n";
    }
  }

  std::filesystem::remove_all(output_directory, error_code);
  return metrics.failed == 0U ? 0 : 1;
}

int run_step32_load_mode(const std::string& executable,
                         const TestClientCliOptions& options,
                         const TestClientProfile& profile) {
  if (options.load_mode != "mass-connect" && options.load_mode != "publish-rate" &&
      options.load_mode != "multi-subscribe") {
    std::cerr << "Unknown Step32 load mode '" << options.load_mode
              << "'. Supported: mass-connect, publish-rate, multi-subscribe\n";
    return 1;
  }

  LoadMetrics metrics;
  metrics.mode = options.load_mode;
  const Clock::time_point started_at = Clock::now();

  int result_code = 1;
  if (metrics.mode == "mass-connect") {
    result_code = run_mass_connect_mode(executable, options, profile, metrics);
  } else if (metrics.mode == "publish-rate") {
    result_code = run_publish_rate_mode(executable, options, profile, metrics);
  } else {
    result_code = run_multi_subscribe_mode(executable, options, profile, metrics);
  }

  finalize_metrics(metrics, started_at);
  print_metrics_summary(metrics, options.load_metrics_json);
  return result_code;
}

const std::vector<BuiltinScenario>& built_in_scenarios() {
  static const std::vector<BuiltinScenario> scenarios = {
      {
          "clean_start_connect_disconnect",
          "Connect, disconnect, reconnect, disconnect",
          {
              {"connect-initial", {"connect"}},
              {"disconnect", {"disconnect"}},
              {"connect-again", {"connect"}},
              {"disconnect-final", {"disconnect"}},
          },
      },
      {
          "qos1_subscribe_publish_unsubscribe",
          "Subscribe once, publish once, unsubscribe",
          {
              {
                  "subscribe",
                  {
                      "subscribe",
                      "--subscription",
                      "step31/qos1|1|false|false|0",
                      "--message-limit",
                      "1",
                      "--wait-timeout-ms",
                      "10000",
                      "--clean-output",
                  },
              },
              {
                  "publish",
                  {
                      "publish",
                      "--topic",
                      "step31/qos1",
                      "--payload",
                      "step31-qos1-message",
                      "--qos",
                      "1",
                  },
              },
              {"unsubscribe", {"unsubscribe", "--topic", "step31/qos1"}},
          },
      },
  };
  return scenarios;
}

const BuiltinScenario& find_scenario_or_throw(const std::string& name) {
  for (const auto& scenario : built_in_scenarios()) {
    if (scenario.name == name) {
      return scenario;
    }
  }
  throw std::invalid_argument("Unknown scenario '" + name + "'.");
}

std::vector<std::string> merge_arguments(
    const std::vector<std::string>& command_arguments,
    const std::vector<std::string>& base_arguments) {
  std::vector<std::string> merged_arguments;
  merged_arguments.reserve(command_arguments.size() + base_arguments.size());
  merged_arguments.insert(merged_arguments.end(), command_arguments.begin(),
                          command_arguments.end());
  merged_arguments.insert(merged_arguments.end(), base_arguments.begin(),
                          base_arguments.end());
  return merged_arguments;
}

} // namespace

int run_test_client_scenario_command(const TestClientCliOptions& options,
                                     const TestClientProfile& profile,
                                     const std::string& executable_path) {
  if (options.list_scenarios) {
    std::cout << "Available scenarios:\n";
    for (const auto& scenario : built_in_scenarios()) {
      std::cout << " - " << scenario.name << ": " << scenario.description
                << "\n";
    }
    std::cout << "\nStep32 load modes:\n"
              << " - mass-connect\n"
              << " - publish-rate\n"
              << " - multi-subscribe\n";
    return 0;
  }

  if (!options.load_mode.empty()) {
    return run_step32_load_mode(executable_path, options, profile);
  }

  const BuiltinScenario& scenario = find_scenario_or_throw(options.scenario_name);
  const std::vector<std::string> base_arguments = make_base_arguments(profile);

  int completed_steps = 0;
  std::cout << "Running scenario '" << scenario.name << "' ("
            << scenario.description << ")\n";

  for (const auto& step : scenario.steps) {
    const std::vector<std::string> command_arguments =
        merge_arguments(step.arguments, base_arguments);

    std::cout << "[STEP] " << step.label << "\n";
    const int step_code = run_command(build_cmdline(executable_path, command_arguments));
    if (step_code != 0) {
      std::cerr << "[FAIL] step '" << step.label << "' exited with code "
                << step_code << "\n";
      return step_code;
    }

    ++completed_steps;
    std::cout << "[PASS] step '" << step.label << "'\n";
  }

  std::cout << "Scenario completed: " << completed_steps << "/"
            << static_cast<int>(scenario.steps.size()) << " steps passed\n";
  return 0;
}

std::vector<std::pair<std::string, std::string>> list_test_client_scenarios() {
  std::vector<std::pair<std::string, std::string>> entries;
  entries.reserve(built_in_scenarios().size());
  for (const auto& scenario : built_in_scenarios()) {
    entries.emplace_back(scenario.name, scenario.description);
  }
  return entries;
}

} // namespace mqtt
