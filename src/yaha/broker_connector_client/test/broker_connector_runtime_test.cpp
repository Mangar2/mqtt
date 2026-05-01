#include <catch2/catch_test_macros.hpp>

#include "yaha/broker_connector_client/broker_connector_runtime.h"

#include <string>
#include <vector>

namespace {

class FakeReceiverPort final : public yaha::ReceiverPublishPort {
public:
    std::vector<std::string>* steps{nullptr};
    bool startResult{true};
    std::string startError{"receiver failed"};
    bool connected{false};

    [[nodiscard]] bool start(std::string& errorMessage) override {
        steps->push_back("receiver-start");
        if (!startResult) {
            errorMessage = startError;
            return false;
        }
        connected = true;
        return true;
    }

    void close() override {
        steps->push_back("receiver-close");
        connected = false;
    }

    [[nodiscard]] bool publish(const yaha::Message& message,
                               const yaha::ReceiverPublishOptions& options,
                               std::string& errorMessage) override {
        (void)message;
        (void)options;
        (void)errorMessage;
        return true;
    }

    [[nodiscard]] bool isConnected() const override {
        return connected;
    }
};

class FakeSourceRuntime final : public yaha::SourceRuntimePort {
public:
    std::vector<std::string>* steps{nullptr};

    void run() override {
        steps->push_back("source-run");
    }

    void close() override {
        steps->push_back("source-close");
    }
};

class FakeConnectorRuntime final : public yaha::ConnectorRuntimePort {
public:
    std::vector<std::string>* steps{nullptr};

    void run() override {
        steps->push_back("connector-run");
    }

    void close() override {
        steps->push_back("connector-close");
    }
};

} // namespace

TEST_CASE("runtime_start_and_close_follow_required_order", "[broker_connector_client]") {
    FakeReceiverPort receiver{};
    FakeSourceRuntime source{};
    FakeConnectorRuntime connector{};

    std::vector<std::string> steps{};
    receiver.steps = &steps;
    source.steps = &steps;
    connector.steps = &steps;

    yaha::BrokerConnectorClientRuntime runtime{receiver, source, connector};

    std::string errorMessage{};
    REQUIRE(runtime.start(errorMessage));
    REQUIRE(runtime.isRunning());

    runtime.close();
    REQUIRE_FALSE(runtime.isRunning());

    REQUIRE(steps.size() == 6U);
    REQUIRE(steps[0] == "receiver-start");
    REQUIRE(steps[1] == "connector-run");
    REQUIRE(steps[2] == "source-run");
    REQUIRE(steps[3] == "source-close");
    REQUIRE(steps[4] == "receiver-close");
    REQUIRE(steps[5] == "connector-close");
}

TEST_CASE("runtime_start_propagates_receiver_start_failure", "[broker_connector_client]") {
    FakeReceiverPort receiver{};
    receiver.startResult = false;
    receiver.startError = "cannot connect receiver";

    std::vector<std::string> steps{};
    receiver.steps = &steps;
    FakeSourceRuntime source{};
    FakeConnectorRuntime connector{};
    source.steps = &steps;
    connector.steps = &steps;

    yaha::BrokerConnectorClientRuntime runtime{receiver, source, connector};

    std::string errorMessage{};
    REQUIRE_FALSE(runtime.start(errorMessage));
    REQUIRE(errorMessage == "cannot connect receiver");
    REQUIRE_FALSE(runtime.isRunning());
    REQUIRE(steps.size() == 1U);
    REQUIRE(steps[0] == "receiver-start");
}
