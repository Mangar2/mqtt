#include <catch2/catch_test_macros.hpp>

#include "json/json_value.h"
#include "yaha/serial_device/serial_device_component.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint32_t k_poll_max_tries{100U};
constexpr std::chrono::milliseconds k_short_wait{10};
constexpr std::uint32_t k_open_fail_retry_count{1U};
constexpr double k_non_integer_value{1.5};

class FakeSerialTransport final : public yaha::ISerialDeviceTransport {
public:
    void open(const std::string& portName, std::uint32_t baudrate) override {
        (void)portName;
        (void)baudrate;
        openCalls += 1U;
        if (openFailCount > 0U) {
            openFailCount -= 1U;
            throw std::runtime_error{"open failed"};
        }
        openState = true;
    }

    void close() override {
        if (closeThrows) {
            throw std::runtime_error{"close failed"};
        }
        openState = false;
    }

    void sendData(const std::string& payloadText) override {
        sendCalls += 1U;
        sent.push_back(payloadText);

        if ((payloadText != "at" || failAtSends) && sendFailCount > 0U) {
            sendFailCount -= 1U;
            throw std::runtime_error{"send failed"};
        }
    }

    void listAvailablePorts() override {
        listCalls += 1U;
    }

    void setReceiveCallback(ReceiveCallback callbackValue) override {
        callback = std::move(callbackValue);
    }

    [[nodiscard]] bool isOpen() const override {
        return openState;
    }

    void emit(const std::string& payloadText) {
        if (callback) {
            const std::vector<std::uint8_t> bytes{payloadText.begin(), payloadText.end()};
            callback(bytes);
        }
    }

    std::vector<std::string> sent{};
    std::uint32_t sendCalls{0U};
    std::uint32_t openCalls{0U};
    std::uint32_t listCalls{0U};
    std::uint32_t sendFailCount{0U};
    std::uint32_t openFailCount{0U};
    bool closeThrows{false};
    bool failAtSends{false};

private:
    ReceiveCallback callback{};
    bool openState{false};
};

[[nodiscard]] std::string readTextFile(const std::filesystem::path& pathValue) {
    std::ifstream stream{pathValue};
    REQUIRE(stream.good());

    std::stringstream buffer{};
    buffer << stream.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::string compactJsonText(const std::string& inputText) {
    std::string outputText{};
    outputText.reserve(inputText.size());

    bool inString = false;
    bool escaping = false;
    for (const char characterValue : inputText) {
        if (inString) {
            outputText.push_back(characterValue);
            if (escaping) {
                escaping = false;
                continue;
            }
            if (characterValue == '\\') {
                escaping = true;
                continue;
            }
            if (characterValue == '"') {
                inString = false;
            }
            continue;
        }

        if (characterValue == '"') {
            inString = true;
            outputText.push_back(characterValue);
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(characterValue)) == 0) {
            outputText.push_back(characterValue);
        }
    }

    return outputText;
}

[[nodiscard]] std::filesystem::path resolveOracleFixturePath(const std::string& relativePath) {
    const std::vector<std::filesystem::path> candidates{
        std::filesystem::path{"test/oracle/serialdevice/data"} / relativePath,
        std::filesystem::path{"../test/oracle/serialdevice/data"} / relativePath,
        std::filesystem::path{"../../test/oracle/serialdevice/data"} / relativePath,
        std::filesystem::path{__FILE__}.parent_path() / ("../../../../test/oracle/serialdevice/data/" + relativePath),
        std::filesystem::path{__FILE__}.parent_path() / ("../../../../../test/oracle/serialdevice/data/" + relativePath)
    };

    for (const auto& candidatePath : candidates) {
        if (std::filesystem::exists(candidatePath)) {
            return std::filesystem::canonical(candidatePath);
        }
    }

    throw std::runtime_error{"Unable to locate oracle fixture: " + relativePath};
}

[[nodiscard]] yaha::SerialDeviceConfig makeRuntimeConfig() {
    yaha::SerialDeviceConfig config{};
    config.serialPortName = "/dev/null";
    config.baudrate = yaha::k_default_serialdevice_baudrate;
    config.subscribeQos = yaha::Qos::AtLeastOnce;
    config.traceLevel = "errors";
    config.keepAliveDelayInSeconds = 1U;

    yaha::SerialDeviceInterfaceDefinition i2cDefinition{};
    i2cDefinition.commandMap["L"] = "demo/topic";
    i2cDefinition.receiverMap["demo/"] = "3";
    i2cDefinition.receiverMapProvided = true;

    config.interfaces["i2c"] = i2cDefinition;
    return config;
}

} // namespace

TEST_CASE("serial_device_runtime_keepalive_matches_oracle_f_fixture", "[serial_device][parity]") {
    const auto fixturePath = resolveOracleFixturePath("F-runtime-slices/keepalive.json");
    const auto fixtureJson = mqtt::json::JsonValue::parse(compactJsonText(readTextFile(fixturePath)));

    const auto caseValue = fixtureJson.at("cases").as_array().at(0);
    REQUIRE(caseValue.at("id").as_string() == "F-001");

    yaha::SerialDeviceConfig config = makeRuntimeConfig();
    config.keepAliveDelayInSeconds = static_cast<std::uint32_t>(caseValue.at("input").at("keepAliveDelayInSeconds").as_number());

    auto transport = std::make_shared<FakeSerialTransport>();
    std::atomic<std::uint32_t> delayCalls{0U};

    yaha::SerialDeviceComponent component{config, transport, [&delayCalls](std::chrono::milliseconds) {
        delayCalls.fetch_add(1U);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }};

    component.run();
    for (std::uint32_t tries = 0U; tries < k_poll_max_tries && delayCalls.load() == 0U; ++tries) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    component.close();

    CHECK(delayCalls.load() >= 1U);
    REQUIRE_FALSE(transport->sent.empty());
    CHECK(transport->sent.front() == "at");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("serial_device_runtime_publish_set_suffix_roundtrip_matches_oracle_f_fixture", "[serial_device][parity]") {
    const auto fixturePath = resolveOracleFixturePath("F-runtime-slices/keepalive.json");
    const auto fixtureJson = mqtt::json::JsonValue::parse(compactJsonText(readTextFile(fixturePath)));

    const auto caseValue = fixtureJson.at("cases").as_array().at(1);
    REQUIRE(caseValue.at("id").as_string() == "F-002");

    yaha::SerialDeviceConfig config = makeRuntimeConfig();
    yaha::SerialDeviceInterfaceDefinition fs20Definition{};
    fs20Definition.commandMap["1234/1111"] = "demo/topic";
    config.interfaces.clear();
    config.interfaces["fs20"] = fs20Definition;

    auto transport = std::make_shared<FakeSerialTransport>();
    std::vector<yaha::Message> published{};
    std::mutex publishedMutex{};

    yaha::SerialDeviceComponent component{config, transport, [](std::chrono::milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }};
    component.setPublishCallback([&published, &publishedMutex](const yaha::Message& messageValue) {
        std::lock_guard<std::mutex> lock{publishedMutex};
        published.push_back(messageValue);
        return yaha::PublishResult::ok();
    });

    component.run();
    transport->emit(R"({"Hauscode":"1234","Adresse":"1111","Befehl":1})");

    std::this_thread::sleep_for(k_short_wait);
    component.close();

    REQUIRE_FALSE(published.empty());
    bool foundSetTopic = false;
    for (const auto& messageValue : published) {
        if (messageValue.topic() == "demo/topic/set") {
            foundSetTopic = true;
            CHECK(static_cast<std::uint32_t>(messageValue.qos()) == 1U);
            REQUIRE_FALSE(messageValue.reason().empty());
            CHECK(messageValue.reason().front().message == "received from arduino");
        }
    }
    CHECK(foundSetTopic);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("serial_device_runtime_retry_and_trace_control_match_oracle_f_fixture", "[serial_device][parity]") {
    const auto fixturePath = resolveOracleFixturePath("F-runtime-slices/retry-open.json");
    const auto fixtureJson = mqtt::json::JsonValue::parse(compactJsonText(readTextFile(fixturePath)));

    yaha::SerialDeviceConfig config = makeRuntimeConfig();
    yaha::SerialDeviceValueMapDefinition valueMapDefinition{};
    valueMapDefinition.usedBy.emplace_back("L");
    valueMapDefinition.map["X"] = 1;
    config.interfaces["i2c"].valueMap["retryMap"] = valueMapDefinition;

    auto transport = std::make_shared<FakeSerialTransport>();

    yaha::SerialDeviceComponent component{config, transport, [](std::chrono::milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }};
    component.run();
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
    transport->sent.clear();
    transport->sendFailCount = 2U;

    const auto payloadText = std::string{fixtureJson.at("cases").as_array().at(0).at("input").at("payload").as_string()};
    const yaha::Message commandMessage{"demo/topic/set", payloadText};
    component.handleMessage(commandMessage);
    for (std::uint32_t tries = 0U; tries < k_poll_max_tries; ++tries) {
        std::uint32_t payloadSendCalls = 0U;
        for (const auto& sentPayload : transport->sent) {
            if (sentPayload != "at") {
                payloadSendCalls += 1U;
            }
        }
        if (payloadSendCalls >= 3U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    const auto retryCase = fixtureJson.at("cases").as_array().at(0);
    REQUIRE(retryCase.at("id").as_string() == "F-101");
    const auto expectedSendCalls = static_cast<std::uint32_t>(retryCase.at("expected").at("sendCalls").as_number());
    const auto expectedOpenCalls = static_cast<std::uint32_t>(retryCase.at("expected").at("openCalls").as_number());

    std::uint32_t payloadSendCalls = 0U;
    for (const auto& sentPayload : transport->sent) {
        if (sentPayload != "at") {
            payloadSendCalls += 1U;
        }
    }

    CHECK(payloadSendCalls == expectedSendCalls);
    CHECK((transport->openCalls - 1U) == expectedOpenCalls);

    const auto traceCase = fixtureJson.at("cases").as_array().at(1);
    REQUIRE(traceCase.at("id").as_string() == "F-102");
    const yaha::Message traceMessage{
        traceCase.at("input").at("topic").as_string(),
        traceCase.at("input").at("value").as_string()};
    component.handleMessage(traceMessage);
    CHECK(component.traceLevel() == traceCase.at("expected").at("trace").as_string());

    component.close();
}

TEST_CASE("serial_device_runtime_matching_reply_uses_base_topic", "[serial_device][runtime]") {
    yaha::SerialDeviceConfig config = makeRuntimeConfig();
    yaha::SerialDeviceInterfaceDefinition fs20Definition{};
    fs20Definition.commandMap["1234/1111"] = "demo/topic";
    config.interfaces.clear();
    config.interfaces["fs20"] = fs20Definition;

    auto transport = std::make_shared<FakeSerialTransport>();
    std::vector<yaha::Message> published{};

    yaha::SerialDeviceComponent component{config, transport, [](std::chrono::milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }};
    component.setPublishCallback([&published](const yaha::Message& messageValue) {
        published.push_back(messageValue);
        return yaha::PublishResult::ok();
    });

    component.run();
    component.handleMessage(yaha::Message{"demo/topic/set", std::string{"on"}});
    transport->emit(R"({"Hauscode":"1234","Adresse":"1111","Befehl":1})");

    std::this_thread::sleep_for(k_short_wait);
    component.close();

    REQUIRE_FALSE(published.empty());
    bool foundBaseTopic = false;
    for (const auto& messageValue : published) {
        if (messageValue.topic() == "demo/topic") {
            foundBaseTopic = true;
        }
    }
    CHECK(foundBaseTopic);
}

TEST_CASE("serial_device_runtime_matching_reply_merges_request_reason_chain", "[serial_device][runtime]") {
    yaha::SerialDeviceConfig config = makeRuntimeConfig();
    yaha::SerialDeviceInterfaceDefinition fs20Definition{};
    fs20Definition.commandMap["1234/1111"] = "demo/topic";
    config.interfaces.clear();
    config.interfaces["fs20"] = fs20Definition;

    auto transport = std::make_shared<FakeSerialTransport>();
    std::vector<yaha::Message> published{};

    yaha::SerialDeviceComponent component{config, transport, [](std::chrono::milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }};
    component.setPublishCallback([&published](const yaha::Message& messageValue) {
        published.push_back(messageValue.clone());
        return yaha::PublishResult::ok();
    });

    yaha::Message incomingSet{"demo/topic/set", std::string{"on"}};
    incomingSet.addReason("manual trigger", "2026-05-31T09:00:00Z");

    component.run();
    component.handleMessage(incomingSet);
    transport->emit(R"({"Hauscode":"1234","Adresse":"1111","Befehl":1})");
    std::this_thread::sleep_for(k_short_wait);
    component.close();

    REQUIRE_FALSE(published.empty());
    const auto iterator = std::ranges::find_if(published, [](const yaha::Message& messageValue) {
        return messageValue.topic() == "demo/topic";
    });
    REQUIRE(iterator != published.end());

    const auto& reasonEntries = iterator->reason();
    REQUIRE(reasonEntries.size() >= 3U);
    CHECK(reasonEntries[0].message == "received from arduino");
    CHECK(reasonEntries[1].message == "received by serialDevice interface service");
    CHECK(reasonEntries[2].message == "manual trigger");
}

TEST_CASE("serial_device_runtime_logs_incoming_and_outgoing_messages_in_yaha_format", "[serial_device][runtime]") {
    yaha::SerialDeviceConfig config = makeRuntimeConfig();
    yaha::SerialDeviceInterfaceDefinition fs20Definition{};
    fs20Definition.commandMap["1234/1111"] = "demo/topic";
    config.interfaces.clear();
    config.interfaces["fs20"] = fs20Definition;
    config.logIncomingMessages = true;
    config.logOutgoingMessages = true;

    auto transport = std::make_shared<FakeSerialTransport>();
    yaha::SerialDeviceComponent component{config, transport, [](std::chrono::milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }};
    component.setPublishCallback([](const yaha::Message&) {
        return yaha::PublishResult::ok();
    });

    std::ostringstream capturedOutput{};
    std::streambuf* previousBuffer = std::cout.rdbuf(capturedOutput.rdbuf());

    component.run();
    component.handleMessage(yaha::Message{"demo/topic/set", std::string{"on"}});
    transport->emit(R"({"Hauscode":"1234","Adresse":"1111","Befehl":1})");
    std::this_thread::sleep_for(k_short_wait);
    component.close();

    std::cout.rdbuf(previousBuffer);
    const std::string logText = capturedOutput.str();
    REQUIRE(logText.find("component=\"serial_device\" direction=\"incoming\" topic=\"demo/topic/set\"") != std::string::npos);
    REQUIRE(logText.find("component=\"serial_device\" direction=\"outgoing\" topic=\"demo/topic\"") != std::string::npos);
}

TEST_CASE("serial_device_runtime_lifecycle_and_error_paths", "[serial_device][runtime]") {
    yaha::SerialDeviceConfig config = makeRuntimeConfig();
    auto failingOpenTransport = std::make_shared<FakeSerialTransport>();
    failingOpenTransport->openFailCount = k_open_fail_retry_count;
    failingOpenTransport->closeThrows = true;

    yaha::SerialDeviceComponent openFailureComponent{config, failingOpenTransport, [](std::chrono::milliseconds) {}};
    const auto subscriptions = openFailureComponent.getSubscriptions();
    REQUIRE(subscriptions.contains("$SYS/serialdevice/#"));
    openFailureComponent.run();
    CHECK(failingOpenTransport->listCalls >= 1U);

    auto runtimeTransport = std::make_shared<FakeSerialTransport>();
    yaha::SerialDeviceComponent component{config, runtimeTransport, [](std::chrono::milliseconds) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }};

    component.run();
    component.run();

    runtimeTransport->openFailCount = k_open_fail_retry_count;
    runtimeTransport->sendFailCount = 2U;
    runtimeTransport->failAtSends = true;

    component.handleMessage(yaha::Message{"demo/topic/set", std::string{"1"}});
    component.handleMessage(yaha::Message{"demo/topic/set", std::string{"X"}});
    component.handleMessage(yaha::Message{"demo/topic/set", 1.0});
    component.handleMessage(yaha::Message{"demo/topic/set", k_non_integer_value});

    runtimeTransport->emit(R"([3,"X",1][3,"L",1])");

    std::this_thread::sleep_for(k_short_wait);
    component.close();

    CHECK(runtimeTransport->openCalls >= 1U);
    CHECK(runtimeTransport->listCalls >= 1U);
}
