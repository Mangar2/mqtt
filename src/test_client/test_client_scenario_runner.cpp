#include "test_client_scenario_runner.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "client/connection_negotiator.h"
#include "client_api/client_config.h"
#include "codec/packet/connect_codec.h"
#include "codec/packet/control_codec.h"
#include "codec/packet/publish_codec.h"
#include "codec/packet/subscribe_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "codec/read_buffer.h"
#include "codec/write_buffer.h"
#include "network/stream_buffer.h"

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
  arguments.emplace_back("--keep-alive-seconds");
  arguments.emplace_back(std::to_string(profile.keep_alive_seconds));
  arguments.emplace_back("--session-expiry-interval-seconds");
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

ClientConfig make_client_config_for_operation(const TestClientProfile& profile,
                                              const std::string& client_id) {
  if (profile.transport != TestClientTransport::Mqtt) {
    throw std::invalid_argument(
        "Step32 load modes currently support only mqtt transport");
  }

  ClientConfig config;
  config.broker_host = profile.host;
  config.broker_port = profile.port;
  config.transport = ClientTransportType::Tcp;
  config.client_id = client_id;
  config.clean_start = profile.clean_start;
  config.keep_alive_seconds = profile.keep_alive_seconds;
  config.session_expiry_interval_seconds =
      profile.session_expiry_interval_seconds;
  config.receive_maximum = profile.receive_maximum;
  config.topic_alias_maximum = profile.topic_alias_maximum;
  config.operation_timeouts.connect_ms = 5000U;
  config.operation_timeouts.publish_ms = 5000U;
  config.operation_timeouts.subscribe_ms = 5000U;
  config.operation_timeouts.unsubscribe_ms = 5000U;
  config.operation_timeouts.disconnect_ms = 5000U;

  if (profile.username.has_value()) {
    config.credentials.username = profile.username;
  }
  if (profile.password.has_value()) {
    config.credentials.password = profile.password;
  }

  validate_client_config_or_throw(config);
  return config;
}

uint16_t next_packet_id() {
  static std::atomic<uint32_t> sequence{1U};
  const uint32_t raw = sequence.fetch_add(1U);
  const uint16_t folded = static_cast<uint16_t>((raw % 65535U) + 1U);
  return folded;
}

QoS qos_from_u8_or_throw(uint8_t qos_value) {
  if (qos_value == 0U) {
    return QoS::AtMostOnce;
  }
  if (qos_value == 1U) {
    return QoS::AtLeastOnce;
  }
  if (qos_value == 2U) {
    return QoS::ExactlyOnce;
  }
  throw std::invalid_argument("Invalid publish_qos for Step32 load operation");
}

AnyPacket read_next_mqtt_packet_or_throw(TcpConnection& connection,
                                         StreamBuffer& stream_buffer,
                                         uint32_t timeout_ms) {
  connection.set_receive_timeout(timeout_ms);
  std::array<uint8_t, 2048> read_chunk{};

  while (true) {
    if (stream_buffer.has_complete_packet()) {
      const std::vector<uint8_t> packet_bytes = stream_buffer.consume_packet();
      ReadBuffer packet_reader(std::span<const uint8_t>(packet_bytes.data(),
                                                        packet_bytes.size()));
      return read_packet(packet_reader);
    }

    const std::ptrdiff_t bytes_read =
        connection.read(std::span<uint8_t>(read_chunk.data(), read_chunk.size()));
    if (bytes_read > 0) {
      const auto append_result = stream_buffer.append(
          std::span<const uint8_t>(read_chunk.data(),
                                   static_cast<std::size_t>(bytes_read)));
      if (append_result != StreamBufferAppendResult::kOk) {
        throw ClientException(ClientError::ProtocolError,
                              "Step32 read buffer overflow while reading packet");
      }
      continue;
    }

    if (bytes_read == 0) {
      throw ClientException(ClientError::ReadFailed,
                            "Broker closed socket during Step32 operation");
    }

    if (connection.last_read_timed_out()) {
      throw ClientException(ClientError::Timeout,
                            "Timed out waiting for broker packet");
    }

    throw ClientException(ClientError::ReadFailed,
                          "Socket read failed during Step32 operation");
  }
}

void send_disconnect_best_effort(TcpConnection& connection) {
  WriteBuffer disconnect_frame;
  DisconnectPacket disconnect_packet;
  disconnect_packet.reason_code = ReasonCode::Success;
  encode_disconnect(disconnect_frame, disconnect_packet);
  (void)connection.write(std::span<const uint8_t>(disconnect_frame.data(),
                                                  disconnect_frame.size()));
}

int execute_publish_operation_direct(const TestClientProfile& profile,
                                    const std::string& client_id,
                                    const std::string& topic,
                                    const std::string& payload) {
  try {
  TcpConnection connection =
    ConnectionNegotiator::dial_tcp(profile.host, profile.port);
  StreamBuffer stream_buffer;

    const ClientConfig config = make_client_config_for_operation(profile, client_id);
    const ConnectPacket connect_packet = build_connect_packet(config);
    (void)ConnectionNegotiator::negotiate(connection, connect_packet, 5000U);

    PublishPacket publish_packet;
    publish_packet.topic = Utf8String{topic};
    publish_packet.qos = qos_from_u8_or_throw(profile.publish_qos);
    publish_packet.retain = profile.publish_retain;
    publish_packet.dup = false;
    publish_packet.payload = BinaryData::from_string(payload);
    if (publish_packet.qos != QoS::AtMostOnce) {
      publish_packet.packet_id = next_packet_id();
    }

    WriteBuffer publish_frame;
    encode_publish(publish_frame, publish_packet);
    if (!connection.write(
            std::span<const uint8_t>(publish_frame.data(), publish_frame.size()))) {
      return 1;
    }

    if (publish_packet.qos == QoS::AtLeastOnce) {
      const uint16_t expected_packet_id = *publish_packet.packet_id;
      while (true) {
        const AnyPacket packet =
            read_next_mqtt_packet_or_throw(connection, stream_buffer, 5000U);
        if (std::holds_alternative<PubackPacket>(packet)) {
          const PubackPacket& puback_packet = std::get<PubackPacket>(packet);
          if (puback_packet.packet_id == expected_packet_id &&
              !is_error(puback_packet.reason_code)) {
            break;
          }
          if (puback_packet.packet_id == expected_packet_id &&
              is_error(puback_packet.reason_code)) {
            return 1;
          }
        }
        if (std::holds_alternative<DisconnectPacket>(packet)) {
          return 1;
        }
      }
    } else if (publish_packet.qos == QoS::ExactlyOnce) {
      const uint16_t expected_packet_id = *publish_packet.packet_id;
      while (true) {
        const AnyPacket packet =
            read_next_mqtt_packet_or_throw(connection, stream_buffer, 5000U);
        if (!std::holds_alternative<PubrecPacket>(packet)) {
          if (std::holds_alternative<DisconnectPacket>(packet)) {
            return 1;
          }
          continue;
        }

        const PubrecPacket& pubrec_packet = std::get<PubrecPacket>(packet);
        if (pubrec_packet.packet_id != expected_packet_id) {
          continue;
        }
        if (is_error(pubrec_packet.reason_code)) {
          return 1;
        }

        WriteBuffer pubrel_frame;
        PubrelPacket pubrel_packet;
        pubrel_packet.packet_id = expected_packet_id;
        encode_pubrel(pubrel_frame, pubrel_packet);
        if (!connection.write(
                std::span<const uint8_t>(pubrel_frame.data(), pubrel_frame.size()))) {
          return 1;
        }
        break;
      }

      while (true) {
        const AnyPacket packet =
            read_next_mqtt_packet_or_throw(connection, stream_buffer, 5000U);
        if (std::holds_alternative<PubcompPacket>(packet)) {
          const PubcompPacket& pubcomp_packet = std::get<PubcompPacket>(packet);
          if (pubcomp_packet.packet_id == expected_packet_id &&
              !is_error(pubcomp_packet.reason_code)) {
            break;
          }
          if (pubcomp_packet.packet_id == expected_packet_id &&
              is_error(pubcomp_packet.reason_code)) {
            return 1;
          }
        }
        if (std::holds_alternative<DisconnectPacket>(packet)) {
          return 1;
        }
      }
    }

    send_disconnect_best_effort(connection);
    connection.close();
    return 0;
  } catch (...) {
    return 1;
  }
}

int execute_subscribe_once_direct(const TestClientProfile& profile,
                                  const std::string& client_id,
                                  const std::string& topic,
                                  uint32_t timeout_ms) {
  try {
  TcpConnection connection =
    ConnectionNegotiator::dial_tcp(profile.host, profile.port);
  StreamBuffer stream_buffer;

    const ClientConfig config = make_client_config_for_operation(profile, client_id);
    const ConnectPacket connect_packet = build_connect_packet(config);
    (void)ConnectionNegotiator::negotiate(connection, connect_packet, 5000U);

    SubscribePacket subscribe_packet;
    subscribe_packet.packet_id = next_packet_id();
    SubscribeFilter filter;
    filter.topic_filter = Utf8String{topic};
    filter.options.max_qos = QoS::AtMostOnce;
    filter.options.no_local = false;
    filter.options.retain_as_published = false;
    filter.options.retain_handling = 0U;
    subscribe_packet.filters.push_back(filter);

    WriteBuffer subscribe_frame;
    encode_subscribe(subscribe_frame, subscribe_packet);
    if (!connection.write(std::span<const uint8_t>(subscribe_frame.data(),
                                                   subscribe_frame.size()))) {
      return 1;
    }

    const AnyPacket suback_packet =
        read_next_mqtt_packet_or_throw(connection, stream_buffer, 5000U);
    if (!std::holds_alternative<SubackPacket>(suback_packet)) {
      return 1;
    }
    const SubackPacket& suback = std::get<SubackPacket>(suback_packet);
    if (suback.packet_id != subscribe_packet.packet_id) {
      return 1;
    }
    for (const ReasonCode reason_code : suback.reason_codes) {
      if (is_error(reason_code)) {
        return 1;
      }
    }

    while (true) {
      const AnyPacket packet =
          read_next_mqtt_packet_or_throw(connection, stream_buffer, timeout_ms);
      if (std::holds_alternative<PublishPacket>(packet)) {
        const PublishPacket& publish_packet = std::get<PublishPacket>(packet);
        if (publish_packet.qos == QoS::AtLeastOnce &&
            publish_packet.packet_id.has_value()) {
          WriteBuffer puback_frame;
          PubackPacket puback_packet;
          puback_packet.packet_id = *publish_packet.packet_id;
          puback_packet.reason_code = ReasonCode::Success;
          encode_puback(puback_frame, puback_packet);
          (void)connection.write(std::span<const uint8_t>(puback_frame.data(),
                                                          puback_frame.size()));
        }
        break;
      }
      if (std::holds_alternative<DisconnectPacket>(packet)) {
        return 1;
      }
    }

    send_disconnect_best_effort(connection);
    connection.close();
    return 0;
  } catch (...) {
    return 1;
  }
}

void record_load_result(LoadMetrics& metrics,
                        std::mutex& metrics_mutex,
                        const Clock::time_point started_at,
                        const Clock::time_point finished_at,
                        int result_code) {
  std::lock_guard<std::mutex> lock(metrics_mutex);
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

template <typename OperationFn>
int run_parallel_operations(uint32_t operation_count,
                            uint32_t parallelism,
                            uint32_t interval_ms,
                            LoadMetrics& metrics,
                            OperationFn operation_fn) {
  const uint32_t worker_count =
      std::max(1U, std::min(parallelism, std::max(1U, operation_count)));
  std::atomic<uint32_t> next_index{0U};
  std::mutex metrics_mutex;
  std::vector<std::future<void>> workers;
  workers.reserve(worker_count);

  for (uint32_t worker_index = 0U; worker_index < worker_count; ++worker_index) {
    workers.emplace_back(std::async(std::launch::async, [&]() {
      while (true) {
        const uint32_t operation_index = next_index.fetch_add(1U);
        if (operation_index >= operation_count) {
          break;
        }

        const Clock::time_point started_at = Clock::now();
        const int result_code = operation_fn(operation_index);
        const Clock::time_point finished_at = Clock::now();
        record_load_result(metrics, metrics_mutex, started_at, finished_at,
                           result_code);
        sleep_between_operations(interval_ms);
      }
    }));
  }

  for (auto& worker : workers) {
    worker.get();
  }

  return metrics.failed == 0U ? 0 : 1;
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

int run_mass_connect_mode(
    const std::string&,
    const TestClientCliOptions& options,
    const TestClientProfile& profile,
    LoadMetrics& metrics) {
  return run_parallel_operations(
      options.load_connection_count,
      options.load_parallelism,
      options.load_connect_interval_ms,
      metrics,
      [&](uint32_t connection_index) {
    const std::string client_id =
        render_template(options.load_client_template, connection_index);
    const std::string topic =
        render_template(options.load_topic_template, connection_index);
    const std::string payload = make_payload_for_index(connection_index, profile);

    return execute_publish_operation_direct(profile, client_id, topic, payload);
      });
}

int run_publish_rate_mode(
    const std::string&,
    const TestClientCliOptions& options,
    const TestClientProfile& profile,
    LoadMetrics& metrics) {
  const uint32_t operation_count =
      std::max(options.load_publish_limit, options.load_connection_count);

  return run_parallel_operations(
      operation_count,
      options.load_parallelism,
      options.load_message_interval_ms,
      metrics,
      [&](uint32_t publish_index) {
    const uint32_t connection_index =
        publish_index % std::max(options.load_connection_count, 1U);
    const std::string client_id =
        render_template(options.load_client_template, connection_index);
    const std::string topic =
        render_template(options.load_topic_template, connection_index);
    const std::string payload = make_payload_for_index(publish_index, profile);

    return execute_publish_operation_direct(profile, client_id, topic, payload);
      });
}

int run_multi_subscribe_mode(
    const std::string&,
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
    const std::filesystem::path output_file = output_directory /
      ("subscriber-" + std::to_string(subscriber_index) + ".log");

    SubscriberTask task{
      std::async(std::launch::async,
             [profile, topic, client_id]() {
             return execute_subscribe_once_direct(profile,
                               client_id,
                               topic,
                               15000U);
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
      render_template(options.load_client_template, subscriber_index) + "-pub";

    const std::string payload = make_payload_for_index(publish_index, profile);
    const Clock::time_point started_at = Clock::now();
    const int publish_result =
        execute_publish_operation_direct(profile, client_id, topic, payload);
    const Clock::time_point finished_at = Clock::now();
    metrics.attempted += 1U;
    metrics.sample_latencies_ms.push_back(
        static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                finished_at - started_at)
                                .count()));
    if (publish_result == 0) {
      metrics.succeeded += 1U;
    } else {
      metrics.failed += 1U;
    }

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
