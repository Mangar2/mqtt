#include <catch2/catch_test_macros.hpp>

#include "yaha/broker_connector_client/broker_connector_client_app.h"
#include "yaha/broker_connector_client/broker_connector_runtime.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

std::filesystem::path make_temp_directory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("yaha_broker_connector_client_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void remove_directory_quiet(const std::filesystem::path& path) {
    std::error_code error_code{};
    std::filesystem::remove_all(path, error_code);
}

std::filesystem::path write_config_file(const std::filesystem::path& directory,
                                        const std::string& content) {
    const auto path = directory / "connector.ini";
    std::ofstream output{path};
    output << content;
    return path;
}

bool try_load_runtime_config_from_file(const std::filesystem::path& config_path,
                                       yaha::BrokerConnectorClientRuntimeConfig& output,
                                       std::string& error_message) {
    yaha::IniDocument document{};
    if (!yaha::IniDocument::tryLoadFromFile(config_path, document, error_message)) {
        return false;
    }

    return yaha::tryLoadBrokerConnectorClientRuntimeConfigFromIni(document,
                                                                   output,
                                                                   error_message);
}

class FakeReceiverPublishPort final : public yaha::ReceiverPublishPort {
public:
    [[nodiscard]] bool start(std::string& error_message) override {
        start_calls_.fetch_add(1);
        if (!start_result_) {
            error_message = start_error_message_;
            return false;
        }
        connected_ = true;
        return true;
    }

    void close() override {
        close_calls_.fetch_add(1);
        connected_ = false;
    }

    [[nodiscard]] bool publish(const yaha::Message&,
                               const yaha::ReceiverPublishOptions&,
                               std::string&) override {
        return connected_;
    }

    [[nodiscard]] bool isConnected() const override {
        return connected_;
    }

    void set_start_result(const bool start_result, const std::string& error_message) {
        start_result_ = start_result;
        start_error_message_ = error_message;
    }

    [[nodiscard]] int start_calls() const {
        return start_calls_.load();
    }

    [[nodiscard]] int close_calls() const {
        return close_calls_.load();
    }

private:
    bool start_result_{true};
    std::string start_error_message_{"receiver start failed"};
    std::atomic<int> start_calls_{0};
    std::atomic<int> close_calls_{0};
    bool connected_{false};
};

class FakeSourceRuntimePort final : public yaha::SourceRuntimePort {
public:
    void run() override {
        run_calls_.fetch_add(1);
    }

    void close() override {
        close_calls_.fetch_add(1);
    }

    [[nodiscard]] int run_calls() const {
        return run_calls_.load();
    }

    [[nodiscard]] int close_calls() const {
        return close_calls_.load();
    }

private:
    std::atomic<int> run_calls_{0};
    std::atomic<int> close_calls_{0};
};

class FakeConnectorRuntimePort final : public yaha::ConnectorRuntimePort {
public:
    void run() override {
        run_calls_.fetch_add(1);
    }

    void close() override {
        close_calls_.fetch_add(1);
    }

    [[nodiscard]] int run_calls() const {
        return run_calls_.load();
    }

    [[nodiscard]] int close_calls() const {
        return close_calls_.load();
    }

private:
    std::atomic<int> run_calls_{0};
    std::atomic<int> close_calls_{0};
};

} // namespace

TEST_CASE("broker_connector_client_runtime_start_and_close_are_idempotent",
          "[broker_connector_client]") {
    FakeReceiverPublishPort receiver_port{};
    FakeSourceRuntimePort source_runtime{};
    FakeConnectorRuntimePort connector_runtime{};

    yaha::BrokerConnectorClientRuntime runtime{
        receiver_port,
        source_runtime,
        connector_runtime
    };

    std::string error_message{};
    REQUIRE(runtime.start(error_message));
    REQUIRE(runtime.start(error_message));
    REQUIRE(runtime.isRunning());

    runtime.close();
    runtime.close();
    REQUIRE_FALSE(runtime.isRunning());

    REQUIRE(receiver_port.start_calls() == 1);
    REQUIRE(receiver_port.close_calls() == 1);
    REQUIRE(source_runtime.run_calls() == 1);
    REQUIRE(source_runtime.close_calls() == 1);
    REQUIRE(connector_runtime.run_calls() == 1);
    REQUIRE(connector_runtime.close_calls() == 1);
}

TEST_CASE("broker_connector_client_runtime_start_propagates_receiver_error",
          "[broker_connector_client]") {
    FakeReceiverPublishPort receiver_port{};
    receiver_port.set_start_result(false, "injected receiver failure");

    FakeSourceRuntimePort source_runtime{};
    FakeConnectorRuntimePort connector_runtime{};

    yaha::BrokerConnectorClientRuntime runtime{
        receiver_port,
        source_runtime,
        connector_runtime
    };

    std::string error_message{};
    REQUIRE_FALSE(runtime.start(error_message));
    REQUIRE(error_message == "injected receiver failure");
    REQUIRE_FALSE(runtime.isRunning());
    REQUIRE(source_runtime.run_calls() == 0);
    REQUIRE(connector_runtime.run_calls() == 0);
}

TEST_CASE("broker_connector_client_runtime_run_until_signal_stops_cleanly",
          "[broker_connector_client]") {
    FakeReceiverPublishPort receiver_port{};
    FakeSourceRuntimePort source_runtime{};
    FakeConnectorRuntimePort connector_runtime{};

    yaha::BrokerConnectorClientRuntime runtime{
        receiver_port,
        source_runtime,
        connector_runtime
    };

    std::atomic<bool> run_result{false};
    std::string error_message{};

    std::thread runtime_thread([&]() {
        run_result.store(runtime.runUntilSignal(error_message));
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
    while (!runtime.isRunning() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    REQUIRE(runtime.isRunning());

    std::raise(SIGTERM);
    runtime_thread.join();

    REQUIRE(run_result.load());
    REQUIRE(error_message.empty());
    REQUIRE_FALSE(runtime.isRunning());
    REQUIRE(receiver_port.close_calls() >= 1);
    REQUIRE(source_runtime.close_calls() >= 1);
    REQUIRE(connector_runtime.close_calls() >= 1);
}

TEST_CASE("broker_connector_client_config_parses_complete_ini",
          "[broker_connector_client]") {
    const auto temp_directory = make_temp_directory();

    const auto config_path = write_config_file(temp_directory,
        "[sourceHttpBroker]\n"
        "host = source.local\n"
        "port = 8080\n"
        "clientId = source-client\n"
        "listenerHost = 127.0.0.1\n"
        "listenerPort = 18080\n"
        "keepAliveSeconds = 11\n"
        "clean = no\n"
        "\n"
        "[sourceSubscriptions]\n"
        "home/# = 1\n"
        "\n"
        "[receiverMqttBroker]\n"
        "host = receiver.local\n"
        "port = 1884\n"
        "clientId = receiver-client\n"
        "reconnectDelayMs = 222\n"
        "keepAliveSeconds = 12\n"
        "loopSleepMs = 7\n"
        "enableLifecycleTrace = off\n"
        "enableMessageTrace = on\n"
        "\n"
        "[automation]\n"
        "reconnectDelayMs = 333\n"
        "sourceLoopSleepMs = 8\n"
        "sourceKeepAliveIntervalMs = 444\n"
        "maxPublishRetries = 5\n"
        "publishRetryBackoffMs = 9\n"
        "normalizeQosToAtLeastOnce = false\n"
        "retainPassthrough = true\n");

    yaha::BrokerConnectorClientRuntimeConfig config{};
    std::string error_message{};

    REQUIRE(try_load_runtime_config_from_file(config_path, config, error_message));
    REQUIRE(config.sourceConfig.brokerHost == "source.local");
    REQUIRE(config.sourceConfig.brokerPort == 8080U);
    REQUIRE(config.sourceConfig.clientId == "source-client");
    REQUIRE(config.sourceConfig.listenerHost == "127.0.0.1");
    REQUIRE(config.sourceConfig.listenerPort == 18080U);
    REQUIRE(config.sourceConfig.keepAliveSeconds == 11U);
    REQUIRE_FALSE(config.sourceConfig.clean);
    REQUIRE(config.sourceConfig.subscribeTopics.size() == 1U);

    REQUIRE(config.receiverConfig.brokerHost == "receiver.local");
    REQUIRE(config.receiverConfig.brokerPort == 1884U);
    REQUIRE(config.receiverConfig.clientId == "receiver-client");
    REQUIRE(config.receiverConfig.reconnectDelay.count() == 222);
    REQUIRE(config.receiverConfig.keepAliveInterval.count() == 12000);
    REQUIRE(config.receiverConfig.loopSleep.count() == 7);
    REQUIRE_FALSE(config.receiverConfig.enableLifecycleTrace);
    REQUIRE(config.receiverConfig.enableMessageTrace);

    REQUIRE(config.sourceLifecycleConfig.reconnectDelay.count() == 333);
    REQUIRE(config.sourceLifecycleConfig.loopSleep.count() == 8);
    REQUIRE(config.sourceLifecycleConfig.keepAliveInterval.count() == 444);
    REQUIRE(config.relayPolicyConfig.maxPublishRetries == 5U);
    REQUIRE(config.relayPolicyConfig.publishRetryBackoff.count() == 9);
    REQUIRE_FALSE(config.relayPolicyConfig.normalizeQosToAtLeastOnce);
    REQUIRE(config.relayPolicyConfig.retainPassthrough);

    remove_directory_quiet(temp_directory);
}

TEST_CASE("broker_connector_client_config_applies_keepalive_fallback_and_monitoring_trace",
          "[broker_connector_client]") {
    const auto temp_directory = make_temp_directory();

    const auto config_path = write_config_file(temp_directory,
        "[sourceHttpBroker]\n"
        "host = source.local\n"
        "keepAliveSeconds = 13\n"
        "\n"
        "[receiverMqttBroker]\n"
        "host = receiver.local\n"
        "\n"
        "[automation]\n"
        "reconnectDelayMs = 100\n"
        "sourceLoopSleepMs = 10\n"
        "maxPublishRetries = 1\n"
        "publishRetryBackoffMs = 5\n"
        "normalizeQosToAtLeastOnce = true\n"
        "retainPassthrough = false\n"
        "\n"
        "[monitoring]\n"
        "sourceLifecycleTrace = true\n");

    yaha::BrokerConnectorClientRuntimeConfig config{};
    std::string error_message{};

    REQUIRE(try_load_runtime_config_from_file(config_path, config, error_message));
    REQUIRE(config.sourceLifecycleConfig.keepAliveInterval.count() == 13000);
    REQUIRE(config.sourceLifecycleConfig.enableTrace);

    remove_directory_quiet(temp_directory);
}

TEST_CASE("broker_connector_client_config_rejects_invalid_boolean",
          "[broker_connector_client]") {
    const auto temp_directory = make_temp_directory();
    const auto config_path = write_config_file(temp_directory,
        "[receiverMqttBroker]\n"
        "enableMessageTrace = maybe\n");

    yaha::BrokerConnectorClientRuntimeConfig config{};
    std::string error_message{};

    REQUIRE_FALSE(try_load_runtime_config_from_file(config_path, config, error_message));
    REQUIRE(error_message.find("receiverMqttBroker.enableMessageTrace") != std::string::npos);

    remove_directory_quiet(temp_directory);
}
