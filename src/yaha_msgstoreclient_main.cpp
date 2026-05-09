#include "yaha/message_store_client/message_store_client_app.h"
#include "yaha/mqtt_client/broker_transport.h"
#include "yaha/mqtt_client/mqtt_client_runtime.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

#ifndef YAHA_MSGSTORECLIENT_VERSION
#define YAHA_MSGSTORECLIENT_VERSION "0.1.10"
#endif

constexpr const char* k_msgstore_client_name{"yahamsgstoreclient"};

struct CliOptions {
    std::filesystem::path configPath{"broker.ini"};
    bool configPathProvided{false};
    bool enableMessageTrace{false};
    bool showHelp{false};
    bool showVersion{false};
};

void printVersion() {
    std::cout << k_msgstore_client_name << ' ' << YAHA_MSGSTORECLIENT_VERSION << '\n' << std::flush;
}

void printUsage() {
    std::cout << "Usage: yahamsgstoreclient [config-path] [--trace-messages] [--version] [--help]\n"
              << "  config-path         optional INI config file (default: broker.ini)\n"
              << "  --trace-messages    print sent/received MQTT messages\n"
              << "  --version           print version and exit\n"
              << "  --help              print this help and exit\n"
              << std::flush;
}

bool tryParseCli(int argc, char* argv[], CliOptions& options, std::string& errorText) {
    for (int argIndex = 1; argIndex < argc; ++argIndex) {
        const std::string argument{argv[argIndex]};
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

std::string valueToText(const yaha::Value& value) {
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    }

    std::ostringstream stream{};
    stream << std::get<double>(value);
    return stream.str();
}

std::string escapeLogText(const std::string_view text) {
    std::string escaped{};
    escaped.reserve(text.size());
    for (const char character : text) {
        switch (character) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(character);
                break;
        }
    }
    return escaped;
}

void traceIncomingMessage(const yaha::Message& message, const bool includeReason) {
    std::cout << "  broker: recv topic=" << message.topic()
              << " qos=" << qosToText(message.qos())
              << " retain=" << (message.retain() ? "1" : "0")
              << " value=" << valueToText(message.value());

    if (includeReason) {
        const std::string reasonText =
            message.reason().empty() ? "none" : escapeLogText(message.reason().front().message);
        std::cout << " reason=\"" << reasonText << '"';
    }

    std::cout << '\n' << std::flush;
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
    if (!tryParseCli(argc, argv, cliOptions, cliError)) {
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
