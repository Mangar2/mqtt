#include "yaha/message_store_client/message_store_client_app.h"
#include "yaha/message/message_payload_codec.h"
#include "yaha/message/message_log_service.h"
#include "yaha/mqtt_client/broker_transport.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <cctype>
#include <utility>

namespace {

#ifndef YAHA_MSGSTORECLIENT_VERSION
#define YAHA_MSGSTORECLIENT_VERSION "0.1.10"
#endif

constexpr const char* k_msgstore_client_name{"yahamsgstoreclient"};

struct CliOptions {
    std::filesystem::path configPath{"broker.ini"};
    std::optional<std::filesystem::path> testInputPath{};
    bool configPathProvided{false};
    bool enableMessageTrace{false};
    bool showHelp{false};
    bool showVersion{false};
};

void printVersion() {
    std::cout << k_msgstore_client_name << ' ' << YAHA_MSGSTORECLIENT_VERSION << '\n' << std::flush;
}

void printUsage() {
    std::cout << "Usage: yahamsgstoreclient [config-path] [--trace-messages] [--test <input-file>] [--version] [--help]\n"
              << "  config-path         optional INI config file (default: broker.ini)\n"
              << "  --trace-messages    print sent/received MQTT messages\n"
              << "  --test <input-file> process JSONL-envelope input synchronously and exit\n"
              << "  --version           print version and exit\n"
              << "  --help              print this help and exit\n"
              << std::flush;
}

bool tryParseTestInputLine(const std::string& lineText,
                           std::optional<yaha::Message>& outputMessage,
                           std::string& errorText) {
    outputMessage.reset();
    if (lineText.empty() || lineText.front() == '#') {
        return true;
    }

    const auto firstNonWhitespace = std::ranges::find_if_not(
        lineText.begin(),
        lineText.end(),
        [](const unsigned char inputChar) { return std::isspace(inputChar) != 0; });
    if (firstNonWhitespace == lineText.end()) {
        return true;
    }

    if (*firstNonWhitespace == '{') {
        constexpr std::string_view topicMarker{R"("topic":")"};
        const std::size_t markerPos = lineText.find(topicMarker);
        if (markerPos == std::string::npos) {
            errorText = "JSONL envelope is missing message.topic";
            return false;
        }

        const std::size_t topicStart = markerPos + topicMarker.size();
        const std::size_t topicEnd = lineText.find('"', topicStart);
        if (topicEnd == std::string::npos || topicEnd == topicStart) {
            errorText = "JSONL envelope contains invalid message.topic";
            return false;
        }

        const std::string mqttTopic = lineText.substr(topicStart, topicEnd - topicStart);
        const auto parsedEnvelope = yaha::parseEnvelopePayload(
            lineText,
            mqttTopic,
            yaha::Qos::AtLeastOnce,
            false,
            false);
        if (!parsedEnvelope.has_value()) {
            errorText = "JSONL envelope parsing failed";
            return false;
        }

        outputMessage = *parsedEnvelope;
        return true;
    }

    errorText = "expected JSONL envelope line starting with '{'";
    return false;
}

int runSynchronousTestMode(const std::filesystem::path& inputPath) {
    std::ifstream inputStream{inputPath};
    if (!inputStream.is_open()) {
        std::cerr << "Failed to open test input file '" << inputPath.string() << "'\n";
        return 1;
    }

    yaha::MessageStoreConfig storeConfig{};
    storeConfig.serverPort = 0U;
    yaha::MessageStore store{std::move(storeConfig)};

    std::uint64_t processedMessages = 0U;
    std::string lineText{};
    std::uint64_t lineNumber = 0U;

    const auto buildStartTime = std::chrono::steady_clock::now();
    while (std::getline(inputStream, lineText)) {
        lineNumber += 1U;

        std::optional<yaha::Message> parsedMessage{};
        std::string parseError{};
        if (!tryParseTestInputLine(lineText, parsedMessage, parseError)) {
            std::cerr << "Failed to parse line " << lineNumber << ": " << parseError << '\n';
            return 1;
        }

        if (!parsedMessage.has_value()) {
            continue;
        }

        store.storeMessageDirect(*parsedMessage);
        processedMessages += 1U;
    }
    const auto buildEndTime = std::chrono::steady_clock::now();

    const auto saveStartTime = std::chrono::steady_clock::now();
    const std::optional<std::filesystem::path> snapshotPath = store.persistSnapshotNow();
    const auto saveEndTime = std::chrono::steady_clock::now();

    const auto buildElapsedMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(buildEndTime - buildStartTime).count();
    const auto saveElapsedMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(saveEndTime - saveStartTime).count();
    const yaha::MessageTree::CompressionStats compressionStats = store.queryCompressionStats();
    std::cout << "test.mode=sync messages=" << processedMessages
              << " buildElapsedMs=" << buildElapsedMilliseconds << '\n' << std::flush;
    std::cout << "test.save"
              << " success=" << (snapshotPath.has_value() ? "1" : "0")
              << " saveElapsedMs=" << saveElapsedMilliseconds;
    if (snapshotPath.has_value()) {
        std::cout << " file=" << snapshotPath->string();
    }
    std::cout << '\n' << std::flush;
    std::cout << "test.stats"
              << " currentNodes=" << compressionStats.currentNodeCount
              << " totalStoredMessages=" << compressionStats.totalStoredMessageCount
              << " historyBuckets=" << compressionStats.historyBucketCount
              << " buckets.single=" << compressionStats.singleBucketCount
              << " buckets.timeValue=" << compressionStats.timeValueBucketCount
              << " buckets.time=" << compressionStats.timeBucketCount
              << " buckets.interval=" << compressionStats.intervalBucketCount
              << " represented.single=" << compressionStats.representedSingleCount
              << " represented.timeValue=" << compressionStats.representedTimeValueCount
              << " represented.time=" << compressionStats.representedTimeCount
              << " represented.interval=" << compressionStats.representedIntervalCount
              << '\n' << std::flush;
    return 0;
}

bool tryParseCli(const std::span<char*> arguments,
                 CliOptions& options,
                 std::string& errorText) {
    for (std::size_t argIndex = 1U; argIndex < arguments.size(); ++argIndex) {
        const std::string argument{arguments[argIndex]};
        if (argument == "--help" || argument == "-h") {
            options.showHelp = true;
            continue;
        }

        if (argument == "--version" || argument == "-V") {
            options.showVersion = true;
            continue;
        }

        if (argument == "--trace-messages") {
            options.enableMessageTrace = true;
            continue;
        }

        if (argument == "--test") {
            if (argIndex + 1U >= arguments.size()) {
                errorText = "--test requires an input filename";
                return false;
            }

            if (options.testInputPath.has_value()) {
                errorText = "--test was provided multiple times";
                return false;
            }

            options.testInputPath = std::filesystem::path{arguments[argIndex + 1U]};
            argIndex += 1U;
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            errorText = "unknown argument: " + argument;
            return false;
        }

        if (options.configPathProvided) {
            errorText = "multiple config paths provided";
            return false;
        }

        options.configPath = std::filesystem::path{argument};
        options.configPathProvided = true;
    }

    return true;
}

const char* qosToText(const yaha::Qos qos) {
    switch (qos) {
        case yaha::Qos::AtMostOnce:
            return "0";
        case yaha::Qos::AtLeastOnce:
            return "1";
        case yaha::Qos::ExactlyOnce:
            return "2";
    }

    return "0";
}

void traceIncomingMessage(const yaha::Message& message, const bool includeReason) {
    const yaha::MessageLogConfig logConfig{
        .enableIncoming = true,
        .enableOutgoing = false,
        .includeReasonChain = includeReason,
    };

    if (const auto line = yaha::buildMessageLogLine(
            "message_store_client",
            yaha::MessageLogDirection::Incoming,
            message,
            logConfig);
        line.has_value()) {
        std::cout << *line << '\n' << std::flush;
    }
}

class IncomingMessageLoggingComponent final : public yaha::IMqttComponent {
public:
    explicit IncomingMessageLoggingComponent(yaha::MessageStore& store, const bool includeReason)
        : store_(store)
        , includeReason_(includeReason) {}

    [[nodiscard]] yaha::SubscriptionMap getSubscriptions() const override {
        return store_.getSubscriptions();
    }

    void handleMessage(const yaha::Message& message) override {
        traceIncomingMessage(message, includeReason_);
        store_.handleMessage(message);
    }

    void run() override {
        store_.run();
    }

    void close() override {
        store_.close();
    }

    void setPublishCallback(yaha::PublishCallback callback) override {
        store_.setPublishCallback(std::move(callback));
    }

private:
    yaha::MessageStore& store_;
    bool includeReason_{true};
};

void printStartupConfiguration(const std::filesystem::path& configPath,
                               const yaha::MessageStoreClientRuntimeConfig& runtimeConfig,
                               const bool useIncomingLogAdapter) {
    std::cout << k_msgstore_client_name << ' ' << YAHA_MSGSTORECLIENT_VERSION << '\n';
    std::cout << "  config: " << configPath.string() << '\n';
    std::cout << "  mqtt: " << runtimeConfig.mqttConfig.brokerHost << ':'
              << runtimeConfig.mqttConfig.brokerPort
              << " clientId=" << runtimeConfig.mqttConfig.clientId << '\n';
    std::cout << "  http: " << runtimeConfig.storeConfig.serverHost << ':'
              << runtimeConfig.storeConfig.serverPort << runtimeConfig.storeConfig.serverPath << '\n';
    std::cout << "  persist: " << runtimeConfig.storeConfig.persistenceConfig.directory << '/'
              << runtimeConfig.storeConfig.persistenceConfig.filename
              << " intervalMs=" << runtimeConfig.storeConfig.persistenceConfig.intervalMs
              << " keepFiles=" << runtimeConfig.storeConfig.persistenceConfig.keepFiles << '\n';
    std::cout << "  subscriptions:";
    for (const auto& subscription : runtimeConfig.storeConfig.subscriptions) {
        std::cout << " [" << subscription.first << "=>qos" << qosToText(subscription.second) << ']';
    }
    std::cout << '\n';
    std::cout << "  startup: connecting broker and applying configured subscriptions\n";
    std::cout << "  startup: broker connection/subscription errors are logged by mqtt runtime\n";
    if (runtimeConfig.mqttConfig.enableMessageTrace) {
        std::cout << "  startup: mqtt sent/received trace enabled\n";
    }
    if (useIncomingLogAdapter) {
        std::cout << "  startup: incoming message logging enabled via messagestore.logIncomingMessages\n";
    }
}

} // namespace

int main(int argc, char* argv[]) {
    CliOptions cliOptions{};
    std::string cliError{};
    if (!tryParseCli(std::span<char*>{argv, static_cast<std::size_t>(argc)}, cliOptions, cliError)) {
        std::cerr << "Failed to parse arguments: " << cliError << '\n';
        printUsage();
        return 1;
    }

    if (cliOptions.showHelp) {
        printUsage();
        return 0;
    }

    if (cliOptions.showVersion) {
        printVersion();
        return 0;
    }

    if (cliOptions.testInputPath.has_value()) {
        return runSynchronousTestMode(*cliOptions.testInputPath);
    }

    const std::filesystem::path configPath = cliOptions.configPath;

    yaha::IniDocument configDocument{};
    std::string errorMessage{};
    try {
        configDocument = yaha::IniDocument::loadFromFile(configPath);
    } catch (const std::exception& exceptionValue) {
        std::cerr << "Failed to load config file '" << configPath.string()
                  << "': " << exceptionValue.what() << '\n';
        return 1;
    }

    yaha::MessageStoreClientRuntimeConfig runtimeConfig{};
    if (!yaha::tryLoadMessageStoreClientRuntimeConfigFromIni(
            configDocument,
            runtimeConfig,
            errorMessage)) {
        std::cerr << "Failed to load MessageStore config from '" << configPath.string()
                  << "': " << errorMessage << '\n';
        return 1;
    }

    runtimeConfig.mqttConfig.enableMessageTrace = cliOptions.enableMessageTrace;
    const bool useIncomingLogAdapter =
        runtimeConfig.logIncomingMessages && !runtimeConfig.mqttConfig.enableMessageTrace;

    const std::string configuredHttpHost = runtimeConfig.storeConfig.serverHost;
    const std::string configuredHttpPath = runtimeConfig.storeConfig.serverPath;
    const std::uint16_t configuredHttpPort = runtimeConfig.storeConfig.serverPort;

    printStartupConfiguration(configPath, runtimeConfig, useIncomingLogAdapter);

    yaha::MessageStore store{std::move(runtimeConfig.storeConfig)};
    std::optional<IncomingMessageLoggingComponent> incomingLogComponent{};
    yaha::IMqttComponent* mqttComponent = &store;
    if (useIncomingLogAdapter) {
        incomingLogComponent.emplace(store, runtimeConfig.logReason);
        mqttComponent = &*incomingLogComponent;
    }

    yaha::YahaMqttClient mqttClient{
        std::move(runtimeConfig.mqttConfig),
        *mqttComponent,
        yaha::makeBrokerTransport()};

    std::cout << "  runtime: started\n";
    if (configuredHttpPort == 0U) {
        std::cout << "  http: disabled (server.port=0)\n";
    } else {
        const std::string effectiveHost =
            configuredHttpHost.empty() ? "127.0.0.1" : configuredHttpHost;
        std::cout << "  http: listening on http://" << effectiveHost << ':' << configuredHttpPort
                  << configuredHttpPath << "\n";
    }
    std::cout << "  signal: waiting for SIGINT/SIGTERM\n";
    std::cout << std::flush;

    yaha::YahaMqttClientRuntime runtime{mqttClient, store};
    runtime.runUntilSignal();

    std::cout << "  signal: received, disconnecting\n";
    std::cout << "  runtime: shutting down\n";
    std::cout << "  runtime: stopped\n";
    std::cout << std::flush;
    return 0;
}
