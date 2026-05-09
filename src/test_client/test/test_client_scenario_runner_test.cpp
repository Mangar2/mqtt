#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <cstdint>
#include <vector>

#include "codec/packet/connect_codec.h"
#include "codec/packet/control_codec.h"
#include "codec/packet/publish_codec.h"
#include "codec/packet/subscribe_codec.h"
#include "codec/packet_reader/packet_reader.h"
#include "codec/read_buffer.h"
#include "codec/write_buffer.h"
#include "network/stream_buffer.h"
#include "network/tcp_listener.h"
#include "test_client/test_client_scenario_runner.h"

namespace mqtt {
namespace {

class FakeScenarioMqttBroker {
public:
  FakeScenarioMqttBroker() = default;

  ~FakeScenarioMqttBroker() {
    stop();
  }

  void start() {
    listener_.emplace(TcpListener::listen(0U));
    running_ = true;
    accept_thread_ = std::thread([this]() {
      accept_loop();
    });
  }

  void stop() {
    if (!running_) {
      return;
    }
    running_ = false;
    if (listener_.has_value()) {
      listener_->close();
    }

    if (accept_thread_.joinable()) {
      accept_thread_.join();
    }

    std::lock_guard<std::mutex> lock{client_threads_mutex_};
    for (std::thread &client_thread : client_threads_) {
      if (client_thread.joinable()) {
        client_thread.join();
      }
    }
    client_threads_.clear();
  }

  [[nodiscard]] std::uint16_t port() const {
    if (!listener_.has_value()) {
      return 0U;
    }
    return listener_->port();
  }

  void set_disconnect_on_publish(const bool enabled) {
    disconnect_on_publish_ = enabled;
  }

private:
  static std::optional<AnyPacket> read_next_packet(
      TcpConnection &connection,
      StreamBuffer &stream_buffer,
      const std::uint32_t timeout_ms) {
    std::array<std::uint8_t, 2048U> read_buffer{};

    while (true) {
      if (stream_buffer.has_complete_packet()) {
        const std::vector<std::uint8_t> packet_bytes =
            stream_buffer.consume_packet();
        ReadBuffer reader{std::span<const std::uint8_t>(
            packet_bytes.data(), packet_bytes.size())};
        return read_packet(reader);
      }

      connection.set_receive_timeout(timeout_ms);
      const std::ptrdiff_t bytes_read = connection.read(read_buffer);
      if (bytes_read == 0) {
        connection.close();
        return std::nullopt;
      }
      if (bytes_read < 0) {
        if (connection.last_read_timed_out()) {
          return std::nullopt;
        }
        connection.close();
        return std::nullopt;
      }

      (void)stream_buffer.append(std::span<const std::uint8_t>(
          read_buffer.data(), static_cast<std::size_t>(bytes_read)));
    }
  }

  static bool send_connack(TcpConnection &connection) {
    ConnackPacket connack_packet{};
    connack_packet.reason_code = ReasonCode::Success;
    WriteBuffer frame{};
    encode_connack(frame, connack_packet);
    return connection.write(
        std::span<const std::uint8_t>(frame.data(), frame.size()));
  }

  static bool send_suback(TcpConnection &connection,
                          const std::uint16_t packet_id) {
    SubackPacket suback_packet{};
    suback_packet.packet_id = packet_id;
    suback_packet.reason_codes = {ReasonCode::GrantedQoS1};
    WriteBuffer frame{};
    encode_suback(frame, suback_packet);
    return connection.write(
        std::span<const std::uint8_t>(frame.data(), frame.size()));
  }

  static bool send_unsuback(TcpConnection &connection,
                            const std::uint16_t packet_id) {
    UnsubackPacket unsuback_packet{};
    unsuback_packet.packet_id = packet_id;
    unsuback_packet.reason_codes = {ReasonCode::Success};
    WriteBuffer frame{};
    encode_unsuback(frame, unsuback_packet);
    return connection.write(
        std::span<const std::uint8_t>(frame.data(), frame.size()));
  }

  static bool send_pingresp(TcpConnection &connection) {
    WriteBuffer frame{};
    encode_pingresp(frame);
    return connection.write(
        std::span<const std::uint8_t>(frame.data(), frame.size()));
  }

  static bool send_publish_to_subscriber(TcpConnection &connection,
                                         const std::string &topic_filter) {
    PublishPacket publish_packet{};
    publish_packet.topic = Utf8String{topic_filter};
    publish_packet.payload = BinaryData::from_string("subscriber-message");
    publish_packet.qos = QoS::AtLeastOnce;
    publish_packet.packet_id = static_cast<std::uint16_t>(31U);

    WriteBuffer frame{};
    encode_publish(frame, publish_packet);
    return connection.write(
        std::span<const std::uint8_t>(frame.data(), frame.size()));
  }

  static bool send_puback(TcpConnection &connection,
                          const std::uint16_t packet_id) {
    PubackPacket puback_packet{};
    puback_packet.packet_id = packet_id;
    puback_packet.reason_code = ReasonCode::Success;
    WriteBuffer frame{};
    encode_puback(frame, puback_packet);
    return connection.write(
        std::span<const std::uint8_t>(frame.data(), frame.size()));
  }

  static bool send_pubrec(TcpConnection &connection,
                          const std::uint16_t packet_id) {
    PubrecPacket pubrec_packet{};
    pubrec_packet.packet_id = packet_id;
    pubrec_packet.reason_code = ReasonCode::Success;
    WriteBuffer frame{};
    encode_pubrec(frame, pubrec_packet);
    return connection.write(
        std::span<const std::uint8_t>(frame.data(), frame.size()));
  }

  static bool send_pubcomp(TcpConnection &connection,
                           const std::uint16_t packet_id) {
    PubcompPacket pubcomp_packet{};
    pubcomp_packet.packet_id = packet_id;
    pubcomp_packet.reason_code = ReasonCode::Success;
    WriteBuffer frame{};
    encode_pubcomp(frame, pubcomp_packet);
    return connection.write(
        std::span<const std::uint8_t>(frame.data(), frame.size()));
  }

  void handle_client(std::unique_ptr<TcpConnection> connection) {
    if (connection == nullptr) {
      return;
    }

    StreamBuffer stream_buffer{};
    bool active = true;

    while (active && running_.load()) {
      const std::optional<AnyPacket> maybe_packet =
          read_next_packet(*connection, stream_buffer, 100U);
      if (!maybe_packet.has_value()) {
        continue;
      }

      if (std::holds_alternative<ConnectPacket>(*maybe_packet)) {
        if (!send_connack(*connection)) {
          break;
        }
        continue;
      }

      if (std::holds_alternative<SubscribePacket>(*maybe_packet)) {
        const SubscribePacket &subscribe_packet =
            std::get<SubscribePacket>(*maybe_packet);
        if (!send_suback(*connection, subscribe_packet.packet_id)) {
          break;
        }
        if (!subscribe_packet.filters.empty()) {
          if (!send_publish_to_subscriber(
                  *connection, subscribe_packet.filters.front().topic_filter.value)) {
            break;
          }
        }
        continue;
      }

      if (std::holds_alternative<UnsubscribePacket>(*maybe_packet)) {
        const UnsubscribePacket &unsubscribe_packet =
            std::get<UnsubscribePacket>(*maybe_packet);
        if (!send_unsuback(*connection, unsubscribe_packet.packet_id)) {
          break;
        }
        continue;
      }

      if (std::holds_alternative<PingreqPacket>(*maybe_packet)) {
        if (!send_pingresp(*connection)) {
          break;
        }
        continue;
      }

      if (std::holds_alternative<PublishPacket>(*maybe_packet)) {
        if (disconnect_on_publish_) {
          connection->close();
          break;
        }

        const PublishPacket &publish_packet = std::get<PublishPacket>(*maybe_packet);
        if (publish_packet.qos == QoS::AtLeastOnce &&
            publish_packet.packet_id.has_value()) {
          if (!send_puback(*connection, *publish_packet.packet_id)) {
            break;
          }
        }
        if (publish_packet.qos == QoS::ExactlyOnce &&
            publish_packet.packet_id.has_value()) {
          if (!send_pubrec(*connection, *publish_packet.packet_id)) {
            break;
          }
        }
        continue;
      }

      if (std::holds_alternative<PubrelPacket>(*maybe_packet)) {
        const PubrelPacket &pubrel_packet = std::get<PubrelPacket>(*maybe_packet);
        if (!send_pubcomp(*connection, pubrel_packet.packet_id)) {
          break;
        }
        continue;
      }

      if (std::holds_alternative<DisconnectPacket>(*maybe_packet)) {
        active = false;
        connection->close();
      }
    }
  }

  void accept_loop() {
    while (running_.load()) {
      try {
        if (!listener_.has_value()) {
          return;
        }
        std::unique_ptr<TcpConnection> connection = listener_->accept();
        std::lock_guard<std::mutex> lock{client_threads_mutex_};
        client_threads_.emplace_back([this, accepted_connection = std::move(connection)]() mutable {
          handle_client(std::move(accepted_connection));
        });
      } catch (...) {
        if (!running_.load()) {
          return;
        }
      }
    }
  }

  std::atomic<bool> running_{false};
  std::optional<TcpListener> listener_{};
  std::thread accept_thread_{};
  std::mutex client_threads_mutex_{};
  std::vector<std::thread> client_threads_{};
  bool disconnect_on_publish_{false};
};

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
    profile.maximum_reconnect_times = 1U;
    profile.reconnect_period_ms = 50U;

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
    profile.maximum_reconnect_times = 1U;
    profile.reconnect_period_ms = 50U;

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
    profile.maximum_reconnect_times = 1U;
    profile.reconnect_period_ms = 50U;

    CHECK(run_test_client_scenario_command(options, profile, "ignored") == 1);
}

TEST_CASE("test_client_scenario_command_step32_mass_connect_mode_succeeds_with_fake_broker",
          "[test_client][scenario]") {
  FakeScenarioMqttBroker broker{};
  broker.start();

  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.load_mode = "mass-connect";
  options.load_connection_count = 3U;
  options.load_parallelism = 1U;
  options.load_connect_interval_ms = 0U;
  options.load_message_interval_ms = 0U;
  options.load_topic_template = "step32/mass/{index}";
  options.load_client_template = "step32-mass-client-{index}";

  TestClientProfile profile;
  profile.host = "127.0.0.1";
  profile.port = broker.port();
  profile.maximum_reconnect_times = 0U;
  profile.reconnect_period_ms = 0U;

  CHECK(run_test_client_scenario_command(options, profile, "ignored") == 0);
  broker.stop();
}

TEST_CASE("test_client_scenario_command_step32_publish_rate_mode_succeeds_with_fake_broker",
          "[test_client][scenario]") {
  FakeScenarioMqttBroker broker{};
  broker.start();

  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.load_mode = "publish-rate";
  options.load_connection_count = 1U;
  options.load_publish_limit = 3U;
  options.load_parallelism = 1U;
  options.load_connect_interval_ms = 0U;
  options.load_message_interval_ms = 0U;
  options.load_verbose = true;
  options.load_metrics_json = true;
  options.load_split_enabled = true;
  options.load_split_delimiter = "|";
  options.load_topic_template = "step32/rate/{index}";
  options.load_client_template = "step32-rate-client-{index}";

  TestClientProfile profile;
  profile.host = "127.0.0.1";
  profile.port = broker.port();
  profile.maximum_reconnect_times = 0U;
  profile.reconnect_period_ms = 0U;
  profile.publish_qos = 2U;
  profile.publish_retain = true;
  profile.publish_dup = true;
  profile.publish_payload = std::string{"414141|424242|434343"};
  profile.publish_payload_encoding = "hex";
  profile.publish_payload_format_indicator = static_cast<std::uint8_t>(1U);
  profile.publish_message_expiry_interval_seconds = 33U;
  profile.publish_topic_alias = 9U;
  profile.publish_response_topic = std::string{"reply/topic"};
  profile.publish_correlation_data = std::string{"QQ=="};
  profile.publish_correlation_data_encoding = "base64";
  profile.publish_subscription_identifier = 77U;
  profile.publish_content_type = std::string{"text/plain"};
  profile.publish_user_properties.emplace_back("prop", "value");

  CHECK(run_test_client_scenario_command(options, profile, "ignored") == 0);
  broker.stop();
}

TEST_CASE("test_client_scenario_command_step32_publish_rate_mode_qos1_succeeds_with_fake_broker",
          "[test_client][scenario]") {
  FakeScenarioMqttBroker broker{};
  broker.start();

  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.load_mode = "publish-rate";
  options.load_connection_count = 1U;
  options.load_publish_limit = 2U;
  options.load_parallelism = 1U;
  options.load_connect_interval_ms = 0U;
  options.load_message_interval_ms = 0U;
  options.load_topic_template = "step32/rate-q1/{index}";
  options.load_client_template = "step32-rate-q1-client-{index}";

  TestClientProfile profile;
  profile.host = "127.0.0.1";
  profile.port = broker.port();
  profile.maximum_reconnect_times = 0U;
  profile.reconnect_period_ms = 0U;
  profile.publish_qos = 1U;

  CHECK(run_test_client_scenario_command(options, profile, "ignored") == 0);
  broker.stop();
}

TEST_CASE("test_client_scenario_command_step32_mass_connect_mode_qos2_succeeds_with_fake_broker",
          "[test_client][scenario]") {
  FakeScenarioMqttBroker broker{};
  broker.start();

  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.load_mode = "mass-connect";
  options.load_connection_count = 2U;
  options.load_parallelism = 1U;
  options.load_connect_interval_ms = 0U;
  options.load_message_interval_ms = 0U;
  options.load_topic_template = "step32/mass-q2/{index}";
  options.load_client_template = "step32-mass-q2-client-{index}";

  TestClientProfile profile;
  profile.host = "127.0.0.1";
  profile.port = broker.port();
  profile.maximum_reconnect_times = 0U;
  profile.reconnect_period_ms = 0U;
  profile.publish_qos = 2U;

  CHECK(run_test_client_scenario_command(options, profile, "ignored") == 0);
  broker.stop();
}

TEST_CASE("test_client_scenario_command_step32_multi_subscribe_mode_succeeds_with_fake_broker",
          "[test_client][scenario]") {
  FakeScenarioMqttBroker broker{};
  broker.start();

  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.load_mode = "multi-subscribe";
  options.load_connection_count = 1U;
  options.load_publish_limit = 1U;
  options.load_parallelism = 1U;
  options.load_connect_interval_ms = 0U;
  options.load_message_interval_ms = 0U;
  options.load_verbose = true;
  options.load_subscribe_qos = 1U;
  options.load_subscribe_no_local = true;
  options.load_subscribe_retain_as_published = true;
  options.load_subscribe_retain_handling = 2U;
  options.load_subscribe_identifier_set = true;
  options.load_subscribe_identifier = 21U;
  options.load_topic_template = "step32/sub/{index}";
  options.load_client_template = "step32-sub-client-{index}";

  TestClientProfile profile;
  profile.host = "127.0.0.1";
  profile.port = broker.port();
  profile.maximum_reconnect_times = 0U;
  profile.reconnect_period_ms = 0U;
  profile.publish_qos = 1U;

  CHECK(run_test_client_scenario_command(options, profile, "ignored") == 0);
  broker.stop();
}

TEST_CASE("test_client_scenario_command_step32_multi_subscribe_rejects_invalid_bench_settings",
          "[test_client][scenario]") {
  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.load_mode = "multi-subscribe";
  options.load_connection_count = 1U;
  options.load_publish_limit = 1U;
  options.load_topic_template = "step32/sub-invalid/{index}";
  options.load_client_template = "step32-sub-invalid-client-{index}";

  TestClientProfile profile;
  profile.host = "127.0.0.1";
  profile.port = 1U;
  profile.maximum_reconnect_times = 0U;
  profile.reconnect_period_ms = 0U;

  options.load_subscribe_qos = 3U;
  CHECK_THROWS_AS(run_test_client_scenario_command(options, profile, "ignored"),
                  std::invalid_argument);

  options.load_subscribe_qos = 1U;
  options.load_subscribe_retain_handling = 3U;
  CHECK_THROWS_AS(run_test_client_scenario_command(options, profile, "ignored"),
                  std::invalid_argument);

  options.load_subscribe_retain_handling = 1U;
  options.load_subscribe_identifier_set = true;
  options.load_subscribe_identifier = 0U;
  CHECK_THROWS_AS(run_test_client_scenario_command(options, profile, "ignored"),
                  std::invalid_argument);
}

TEST_CASE("test_client_scenario_command_step32_publish_rate_mode_fails_when_broker_disconnects_on_publish",
          "[test_client][scenario]") {
  FakeScenarioMqttBroker broker{};
  broker.set_disconnect_on_publish(true);
  broker.start();

  TestClientCliOptions options;
  options.command = TestClientCommand::Scenario;
  options.load_mode = "publish-rate";
  options.load_connection_count = 1U;
  options.load_publish_limit = 2U;
  options.load_parallelism = 1U;
  options.load_connect_interval_ms = 0U;
  options.load_message_interval_ms = 0U;
  options.load_topic_template = "step32/rate-fail/{index}";
  options.load_client_template = "step32-rate-fail-client-{index}";

  TestClientProfile profile;
  profile.host = "127.0.0.1";
  profile.port = broker.port();
  profile.maximum_reconnect_times = 0U;
  profile.reconnect_period_ms = 0U;
  profile.publish_qos = 1U;

  CHECK_THROWS(run_test_client_scenario_command(options, profile, "ignored"));
  broker.stop();
}

} // namespace mqtt
