#include "yaha/ini/ini_document.h"
#include "yaha/mqtt_client/broker_transport.h"
#include "yaha/mqtt_client/mqtt_client.h"
#include "yaha/remote_service/remote_service_component.h"
#include "yaha/remote_service_client/remote_service_client_app.h"
#include "yaha/remote_service_http/remote_service_http_adapter.h"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <thread>

namespace {

constexpr const char* kRemoteServiceClientName{"yaharemoteserviceclient"};
constexpr const char* kRemoteServiceClientVersion{"0.1.0"};
constexpr int kRuntimePollIntervalMs{100};

std::atomic<bool> shutdownRequested{false};

void handleSignal(int signalValue) {
    (void)signalValue;
    shutdownRequested.store(true);
}

struct CliOptions {
    std::filesystem::path configPath{"broker.ini"};
    bool configPathProvided{false};
    bool enableMessageTrace{false};
    bool showHelp{false};
};

void printUsage() {
    std::cout << "Usage: yaharemoteserviceclient [config-path] [--trace-messages] [--help]\n"
              << "  config-path         optional INI config file (default: broker.ini)\n"
              << "  --trace-messages    print sent/received MQTT messages\n"
              << "  --help              print this help and exit\n"
              << std::flush;
}

bool tryParseCli(const int argc, char* argv[], CliOptions& options, std::string& errorText) {
    for (int argIndex = 1; argIndex < argc; ++argIndex) {
        const std::string argument{argv[argIndex]};
        if (argument == "--help" || argument == "-h") {
            options.showHelp = true;
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

void printStartupSummary(
    const std::filesystem::path& configPath,
    const yaha::RemoteServiceClientRuntimeConfig& runtimeConfig) {
    std::cout << kRemoteServiceClientName << ' ' << kRemoteServiceClientVersion << '\n';
    std::cout << "  config: " << configPath.string() << '\n';
    std::cout << "  mqtt: " << runtimeConfig.mqttConfig.brokerHost << ':'
              << runtimeConfig.mqttConfig.brokerPort
              << " clientId=" << runtimeConfig.mqttConfig.clientId << '\n';
    std::cout << "  filestore: host=" << runtimeConfig.remoteServiceConfig.fileStoreHost
              << ':' << runtimeConfig.remoteServiceConfig.fileStorePort
              << " mappingKeyPath=" << runtimeConfig.remoteServiceConfig.mappingKeyPath << '\n';
    std::cout << "  http: listenHost=" << runtimeConfig.remoteServiceConfig.listenHost
              << " listenPort=" << runtimeConfig.remoteServiceConfig.listenPort << '\n';
    std::cout << "  monitor: prefix=" << runtimeConfig.remoteServiceConfig.monitorTopicPrefix
              << " subscribeQos="
              << static_cast<int>(runtimeConfig.remoteServiceConfig.subscribeQos)
              << '\n';
    std::cout << std::flush;
}

std::map<std::string, std::string> extractQueryValues(const httplib::Request& request) {
    std::map<std::string, std::string> queryValues{};
    if (request.has_param("deviceId")) {
        queryValues["deviceId"] = request.get_param_value("deviceId");
    }
    if (request.has_param("state")) {
        queryValues["state"] = request.get_param_value("state");
    }
    if (request.has_param("accessToken")) {
        queryValues["accessToken"] = request.get_param_value("accessToken");
    }

    return queryValues;
}

void applyHttpResponse(const yaha::RemoteServiceHttpResponse& source, httplib::Response& target) {
    target.status = source.statusCode;
    target.set_content(source.payload, source.contentType);
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

    yaha::IniDocument configDocument{};
    try {
        configDocument = yaha::IniDocument::loadFromFile(cliOptions.configPath);
    } catch (const std::exception& exceptionValue) {
        std::cerr << "Failed to load config file '" << cliOptions.configPath.string()
                  << "': " << exceptionValue.what() << '\n';
        return 1;
    }

    yaha::RemoteServiceClientRuntimeConfig runtimeConfig{};
    std::string configError{};
    if (!yaha::tryLoadRemoteServiceClientRuntimeConfigFromIni(
            configDocument,
            runtimeConfig,
            configError)) {
        std::cerr << "Failed to load RemoteService config from '"
                  << cliOptions.configPath.string() << "': " << configError << '\n';
        return 1;
    }

    runtimeConfig.mqttConfig.enableMessageTrace = cliOptions.enableMessageTrace;
    printStartupSummary(cliOptions.configPath, runtimeConfig);

    yaha::RemoteServiceComponent component{runtimeConfig.remoteServiceConfig};
    yaha::RemoteServiceHttpAdapter httpAdapter{component};
    httpAdapter.setAccessTokenValidator([](const std::string& tokenValue) {
        return !tokenValue.empty();
    });
    httpAdapter.setDeviceTokenValidator([](const std::string& tokenValue) {
        return !tokenValue.empty();
    });

    yaha::YahaMqttClient mqttClient{
        std::move(runtimeConfig.mqttConfig),
        component,
        yaha::makeBrokerTransport()};

    httplib::Server httpServer{};
    httpServer.Get(R"(.*)", [&httpAdapter](const httplib::Request& request, httplib::Response& response) {
        const yaha::RemoteServiceHttpResponse adapterResponse =
            httpAdapter.handleGet(request.path, extractQueryValues(request));
        applyHttpResponse(adapterResponse, response);
    });
    httpServer.Post(R"(.*)", [&httpAdapter](const httplib::Request& request, httplib::Response& response) {
        const yaha::RemoteServiceHttpResponse adapterResponse =
            httpAdapter.handlePost(request.path, request.body);
        applyHttpResponse(adapterResponse, response);
    });

    std::thread httpThread{[&httpServer, &runtimeConfig]() {
        const std::string listenHost = runtimeConfig.remoteServiceConfig.listenHost.empty()
            ? "0.0.0.0"
            : runtimeConfig.remoteServiceConfig.listenHost;
        const int listenPort = static_cast<int>(runtimeConfig.remoteServiceConfig.listenPort);
        if (!httpServer.listen(listenHost, listenPort)) {
            std::cerr << "remoteservice_client[error] op=http_listen"
                      << " host=" << listenHost
                      << " port=" << listenPort
                      << " reason=listen_failed"
                      << '\n' << std::flush;
        }
    }};

    shutdownRequested.store(false);
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    component.run();
    mqttClient.run();

    std::cout << "  runtime: started\n";
    std::cout << "  signal: waiting for SIGINT/SIGTERM\n";
    std::cout << std::flush;

    while (!shutdownRequested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{kRuntimePollIntervalMs});
    }

    std::cout << "  signal: received, shutting down\n";
    std::cout << std::flush;

    httpServer.stop();
    if (httpThread.joinable()) {
        httpThread.join();
    }

    mqttClient.close();
    component.close();

    std::cout << "  runtime: stopped\n" << std::flush;
    return 0;
}