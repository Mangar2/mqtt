#include "yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.h"

#include "yaha/http_mqtt_interface/http_mqtt_interface_operations.h"

#include <httplib.h>

#include <atomic>
#include <csignal>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace yaha {

namespace {

constexpr std::string_view k_httpSection{"httpMqttInterface"};
constexpr std::string_view k_listenerHostKey{"listenerHost"};
constexpr std::string_view k_listenerPortKey{"listenerPort"};
constexpr std::string_view k_enablePublishPhpAliasKey{"enablePublishPhpAlias"};
constexpr std::string_view k_useLegacyPhpResponseKey{"useLegacyPhpResponse"};
constexpr std::string_view k_healthEndpoint{"/health"};
constexpr std::string_view k_publishEndpoint{"/publish"};
constexpr std::string_view k_publishPhpEndpoint{"/publish.php"};
constexpr std::string_view k_pubrelEndpoint{"/pubrel"};
constexpr int k_httpStatusOk{200};
constexpr int k_httpStatusNoContent{204};
constexpr std::string_view k_publishCorsMethods{"POST, PUT, OPTIONS"};
constexpr std::string_view k_publishCorsHeaders{"Content-Type, Authorization, X-Requested-With"};

std::atomic<httplib::Server*> g_activeServer{nullptr};

void applyHttpMqttCorsHeaders(httplib::Response& response, const bool includeMaxAge) {
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Access-Control-Allow-Methods", std::string{k_publishCorsMethods});
    response.set_header("Access-Control-Allow-Headers", std::string{k_publishCorsHeaders});
    if (includeMaxAge) {
        response.set_header("Access-Control-Max-Age", "86400");
    }
}

void applyHttpMqttResult(const HttpMqttResult& result, httplib::Response& response) {
    applyHttpMqttCorsHeaders(response, false);
    response.status = result.statusCode;
    for (const auto& [headerName, headerValue] : result.headers) {
        response.set_header(headerName, headerValue);
    }
    response.set_content(result.payload, "application/json");
}

void logIncomingPublishRequest(const httplib::Request& request, const std::string_view endpoint) {
    std::cout << "http_mqtt_interface_client[in] method=" << request.method
              << " endpoint=" << endpoint;

    const auto versionIterator = request.headers.find("version");
    if (versionIterator != request.headers.end()) {
        std::cout << " version=" << versionIterator->second;
    }

    std::cout << '\n' << std::flush;
}

HttpMqttHeaders collectHeaders(const httplib::Request& request) {
    HttpMqttHeaders headers{};
    for (const auto& [headerName, headerValue] : request.headers) {
        headers[headerName] = headerValue;
    }

    return headers;
}

HttpMqttHeaders collectFields(const httplib::Request& request) {
    HttpMqttHeaders fields{};
    for (const auto& [fieldName, fieldValue] : request.params) {
        fields[fieldName] = fieldValue;
    }

    return fields;
}

std::string resolveCompatibilityToken(const httplib::Request& request, const HttpMqttHeaders& fields) {
    const auto tokenHeaderIterator = request.headers.find("token");
    if (tokenHeaderIterator != request.headers.end()) {
        return tokenHeaderIterator->second;
    }

    const auto fieldIterator = fields.find("token");
    if (fieldIterator != fields.end()) {
        return fieldIterator->second;
    }

    return "";
}

HttpMqttResult handleCompatibilityPublish(
    const HttpMqttInterfaces& interfaces,
    const httplib::Request& request,
    const HttpMqttPublishCompatibilityConfig& compatibilityConfig,
    const std::string_view endpoint) {
    const HttpMqttHeaders fields = collectFields(request);
    const HttpMqttPublishCompatibilityRequest compatibilityRequest{
        .method = request.method,
        .endpoint = std::string{endpoint},
        .headers = collectHeaders(request),
        .fields = fields,
        .body = request.body,
        .token = resolveCompatibilityToken(request, fields),
    };

    return handlePublishCompatibilityRequest(
        interfaces,
        compatibilityRequest,
        compatibilityConfig,
        [&interfaces](const HttpMqttRequestData& downstreamRequest) {
            return interfaces.onPublish(downstreamRequest.headers);
        });
}

void stopActiveServerSignalHandler(int /*signalValue*/) {
    httplib::Server* server = g_activeServer.load();
    if (server != nullptr) {
        server->stop();
    }
}

} // namespace

bool tryLoadHttpMqttInterfaceClientConfigFromIni(
    const IniDocument& iniDocument,
    HttpMqttInterfaceClientConfig& configOutput,
    std::string& errorOutput) {
    errorOutput.clear();

    if (const auto maybeHost = iniDocument.lastValue(k_httpSection, k_listenerHostKey); maybeHost.has_value()) {
        configOutput.listenerHost = *maybeHost;
    }

    const auto [maybePort, portError] = iniDocument.readUnsigned(
        k_httpSection,
        k_listenerPortKey,
        1U,
        65535U);
    if (!portError.empty()) {
        errorOutput = portError;
        return false;
    }
    if (maybePort.has_value()) {
        configOutput.listenerPort = static_cast<std::uint16_t>(*maybePort);
    }

    const auto [maybePublishPhpAlias, publishPhpAliasError] = iniDocument.readBool(
        k_httpSection,
        k_enablePublishPhpAliasKey);
    if (!publishPhpAliasError.empty()) {
        errorOutput = publishPhpAliasError;
        return false;
    }
    if (maybePublishPhpAlias.has_value()) {
        configOutput.enablePublishPhpAlias = *maybePublishPhpAlias;
    }

    const auto [maybeLegacyResponse, legacyResponseError] = iniDocument.readBool(
        k_httpSection,
        k_useLegacyPhpResponseKey);
    if (!legacyResponseError.empty()) {
        errorOutput = legacyResponseError;
        return false;
    }
    if (maybeLegacyResponse.has_value()) {
        configOutput.useLegacyPhpResponse = *maybeLegacyResponse;
    }

    return true;
}

int runHttpMqttInterfaceClient(const HttpMqttInterfaceClientConfig& configInput) {
    HttpMqttInterfaces interfaces = makeHttpMqttInterfacesV1();
    const HttpMqttPublishCompatibilityConfig compatibilityConfig{
        .enablePublishPhpAlias = configInput.enablePublishPhpAlias,
        .responseMode = configInput.useLegacyPhpResponse
            ? HttpMqttPublishCompatibilityResponseMode::LegacyPhp
            : HttpMqttPublishCompatibilityResponseMode::Native,
    };

    httplib::Server server{};

    server.Get(k_healthEndpoint.data(), [](const httplib::Request& /*request*/, httplib::Response& response) {
        response.status = k_httpStatusOk;
        response.set_content("ok", "text/plain");
    });

    server.Put(k_publishEndpoint.data(),
               [&interfaces](const httplib::Request& request, httplib::Response& response) {
                   logIncomingPublishRequest(request, k_publishEndpoint);
                   applyHttpMqttResult(interfaces.onPublish(collectHeaders(request)), response);
               });

    server.Put(k_pubrelEndpoint.data(),
               [&interfaces](const httplib::Request& request, httplib::Response& response) {
                   applyHttpMqttResult(interfaces.onPubrel(collectHeaders(request)), response);
               });

    server.Post(k_publishEndpoint.data(),
                [&interfaces, &compatibilityConfig](
                    const httplib::Request& request,
                    httplib::Response& response) {
                    logIncomingPublishRequest(request, k_publishEndpoint);
                    applyHttpMqttResult(
                        handleCompatibilityPublish(
                            interfaces,
                            request,
                            compatibilityConfig,
                            k_publishEndpoint),
                        response);
                });

    server.Post(k_publishPhpEndpoint.data(),
                [&interfaces, &compatibilityConfig](
                    const httplib::Request& request,
                    httplib::Response& response) {
                    logIncomingPublishRequest(request, k_publishPhpEndpoint);
                    applyHttpMqttResult(
                        handleCompatibilityPublish(
                            interfaces,
                            request,
                            compatibilityConfig,
                            k_publishPhpEndpoint),
                        response);
                });

    server.Options(k_publishEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    server.Options(k_publishPhpEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    server.Options(k_pubrelEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    std::signal(SIGINT, stopActiveServerSignalHandler);
    std::signal(SIGTERM, stopActiveServerSignalHandler);

    g_activeServer.store(&server);

    std::cout << "yahahttpmqttinterfaceclient\n";
    std::cout << "  listener: " << configInput.listenerHost << ':' << configInput.listenerPort << '\n';
    std::cout << "  compatibility: publish.php=" << (configInput.enablePublishPhpAlias ? "on" : "off")
              << " legacyResponse=" << (configInput.useLegacyPhpResponse ? "on" : "off") << '\n';
    std::cout << "  signal: waiting for SIGINT/SIGTERM\n";
    std::cout << std::flush;

    const bool listenSuccess = server.listen(configInput.listenerHost, configInput.listenerPort);
    g_activeServer.store(nullptr);

    if (!listenSuccess) {
        std::cerr << "Failed to start HTTP listener on " << configInput.listenerHost << ':'
                  << configInput.listenerPort << '\n';
        return 1;
    }

    return 0;
}

} // namespace yaha
