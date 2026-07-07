#pragma once

#include "yaha/http_mqtt_interface/http_mqtt_interface_operations.h"
#include "yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.h"
#include "json/json_value.h"

#include <httplib.h>

#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yaha::http_mqtt_interface_client_internal {

inline constexpr std::string_view k_httpSection{"httpMqttInterface"};
inline constexpr std::string_view k_listenerHostKey{"listenerHost"};
inline constexpr std::string_view k_listenerPortKey{"listenerPort"};
inline constexpr std::string_view k_enablePublishPhpAliasKey{"enablePublishPhpAlias"};
inline constexpr std::string_view k_useLegacyPhpResponseKey{"useLegacyPhpResponse"};
inline constexpr std::string_view k_logIncomingRequestsKey{"logIncomingRequests"};
inline constexpr std::string_view k_logEventsKey{"logEvents"};
inline constexpr std::string_view k_logErrorsKey{"logErrors"};
inline constexpr std::string_view k_logBrokerMessagesKey{"logBrokerMessages"};
inline constexpr std::string_view k_logReasonKey{"logReason"};
inline constexpr std::string_view k_logTracingKey{"logTracing"};
inline constexpr std::string_view k_connectedClientsReportIntervalSecondsKey{"connectedClientsReportIntervalSeconds"};
inline constexpr std::string_view k_healthEndpoint{"/health"};
inline constexpr std::string_view k_publishEndpoint{"/publish"};
inline constexpr std::string_view k_publishPhpEndpoint{"/publish.php"};
inline constexpr std::string_view k_pubrelEndpoint{"/pubrel"};
inline constexpr std::string_view k_connectEndpoint{"/connect"};
inline constexpr std::string_view k_disconnectEndpoint{"/disconnect"};
inline constexpr std::string_view k_subscribeEndpoint{"/subscribe"};
inline constexpr std::string_view k_unsubscribeEndpoint{"/unsubscribe"};
inline constexpr std::string_view k_pingEndpoint{"/pingreq"};
inline constexpr std::string_view k_receiveEndpoint{"/receive"};
inline constexpr int k_httpStatusOk{200};
inline constexpr int k_httpStatusNoContent{204};
inline constexpr int k_httpStatusBadRequest{400};
inline constexpr int k_httpStatusInternalServerError{500};
inline constexpr int k_legacy_listener_timeout_us{300000};
inline constexpr int k_legacy_dispatch_idle_sleep_ms{25};
inline constexpr int k_connected_clients_report_sleep_step_ms{250};
inline constexpr int k_uint16_max{65535};
inline constexpr long long k_uint32_max{4294967295LL};
inline constexpr std::string_view k_publishCorsMethods{"POST, PUT, OPTIONS"};
inline constexpr std::string_view k_publishCorsHeaders{"Content-Type, Authorization, X-Requested-With"};
inline constexpr const char* k_error_code_broker_publish_failed{"HTTP_MQTT_BROKER_PUBLISH_FAILED"};
inline constexpr const char* k_error_code_listener_start_failed{"HTTP_MQTT_LISTENER_START_FAILED"};
inline constexpr std::string_view k_subscribe_topics_shape_error_prefix =
    R"(request_invalid: expected JSON field 'topics' as object {"topic/filter": qos0..2} or field 'subscribe' as object {"topic/filter": qos0..2} or {"QoS":0..2,"topics":"topic/filter"|["topic/1","topic/2"]}; parse_error=)";
inline constexpr std::string_view k_unsubscribe_topics_shape_error_prefix =
    R"(request_invalid: expected JSON field 'topics' as object {"topic/filter": qos0..2} or field 'unsubscribe' as object {"topic/filter": qos0..2} or {"QoS":0..2,"topics":"topic/filter"|["topic/1","topic/2"]}; parse_error=)";

struct LegacyListenerEndpoint {
    std::string host{};
    std::uint16_t port{0U};
};

void logHttpMqttEvent(bool enabled, std::string_view eventName, std::string_view detailText = "");
void logHttpMqttTrace(bool enabled, std::string_view traceName, std::string_view detailText = "");
void logHttpMqttError(bool enabled, std::string_view operationName, std::string_view reasonText, std::string_view detailText = "");
void logBrokerForwardPublishAck(bool enabled, bool includeReasonChain, const Message& message);
void logBrokerIncomingMessage(bool enabled, bool includeReasonChain, const Message& message);
void logListenerForwardResult(bool eventLoggingEnabled, bool errorLoggingEnabled, std::string_view clientId, std::string_view token, const Message& message, bool forwarded);
void logConnectedClientsReport(bool enabled, const std::vector<HttpMqttSessionSnapshot>& sessions);
void logBrokerForwardPublishError(bool enabled, bool includeReasonChain, const Message& message, std::string_view errorText);
void logBrokerPublishDispatchAttempt(bool enabled, const Message& message, std::string_view token, std::string_view dispatchPath);
void logBrokerPublishDispatchSent(bool enabled, const Message& message, std::string_view token, std::string_view dispatchPath);
void logBrokerPublishDispatchFailed(bool enabled, const Message& message, std::string_view token, std::string_view dispatchPath, std::string_view reasonText);
void logCompatibilityRequestFailure(bool enabled, std::string_view endpoint, std::string_view errorText);
void logCompatibilityInternalResultFailure(bool enabled, std::string_view endpoint, const HttpMqttResult& result);
[[nodiscard]] HttpMqttResult makeCompatibilityInternalErrorResult();
void applyHttpMqttCorsHeaders(httplib::Response& response, bool includeMaxAge);
void applyHttpMqttResult(const HttpMqttResult& result, httplib::Response& response);
void logIncomingPublishRequest(bool enabled, const httplib::Request& request, std::string_view endpoint, std::string_view clientId, std::string_view token, std::string_view topic);
[[nodiscard]] std::string resolveClientIdForRequest(const HttpMqttSessionManager& sessionManager, const std::optional<mqtt::json::JsonValue>& jsonBody, const std::string& token);
[[nodiscard]] std::string resolveTopicForRequest(const HttpMqttHeaders& fields, const std::optional<mqtt::json::JsonValue>& jsonBody);
[[nodiscard]] HttpMqttHeaders collectHeaders(const httplib::Request& request);
[[nodiscard]] HttpMqttHeaders collectFields(const httplib::Request& request);
[[nodiscard]] std::string resolveCompatibilityToken(const httplib::Request& request, const HttpMqttHeaders& fields);
[[nodiscard]] std::optional<mqtt::json::JsonValue> tryParseJsonBody(const httplib::Request& request);
[[nodiscard]] std::optional<std::string> tryReadStringField(const mqtt::json::JsonValue& value, std::string_view fieldName);
[[nodiscard]] std::optional<std::uint16_t> tryReadUInt16Field(const mqtt::json::JsonValue& value, std::string_view fieldName);
[[nodiscard]] std::optional<std::uint32_t> tryReadUInt32Field(const mqtt::json::JsonValue& value, std::string_view fieldName);
[[nodiscard]] std::optional<int> tryReadIntField(const mqtt::json::JsonValue& value, std::string_view fieldName);
[[nodiscard]] std::string resolveToken(const httplib::Request& request, const HttpMqttHeaders& fields, const std::optional<mqtt::json::JsonValue>& jsonBody);
[[nodiscard]] std::map<std::string, Qos> parseTopicsObject(const std::optional<mqtt::json::JsonValue>& jsonBody, std::string& errorText, std::string_view legacyFieldName = "");
[[nodiscard]] std::string buildReceivePayload(const Message& message);
[[nodiscard]] bool forwardLegacyListenerPublish(const LegacyListenerEndpoint& endpoint, const Message& message, const HttpMqttHeaders& requestHeaders);
[[nodiscard]] HttpMqttResult makeJsonErrorResult(int statusCode, std::string_view errorCode);
[[nodiscard]] HttpMqttResult makeNoContentResult(std::string_view packetName = "");
[[nodiscard]] bool ensureSupportedRequestVersion(bool errorLoggingEnabled, std::string_view operationName, const httplib::Request& request, httplib::Response& response);
[[nodiscard]] std::string withRawBodyDetail(const std::string& detailText, const httplib::Request& request);
void applyBoolConfigWithFallback(const IniDocument& iniDocument, bool& configValue, std::string_view sectionName, std::string_view keyName);

} // namespace yaha::http_mqtt_interface_client_internal

namespace yaha {

struct HttpMqttInterfaceClientComponent::Impl {
    explicit Impl(
        HttpMqttInterfaceClientConfig configInput,
        HttpMqttSessionTransportFactory sessionTransportFactory)
        : config(std::move(configInput))
        , interfaces(makeHttpMqttInterfacesV1())
        , sessionManager(config.mqttConfig, std::move(sessionTransportFactory))
        , compatibilityConfig{
            .enablePublishPhpAlias = config.enablePublishPhpAlias,
            .responseMode = config.useLegacyPhpResponse
                ? HttpMqttPublishCompatibilityResponseMode::LegacyPhp
                : HttpMqttPublishCompatibilityResponseMode::Native,
        } {}

    HttpMqttInterfaceClientConfig config{};
    HttpMqttInterfaces interfaces;
    HttpMqttSessionManager sessionManager;
    HttpMqttPublishCompatibilityConfig compatibilityConfig{};
    httplib::Server server{};

    std::mutex publishCallbackMutex{};
    PublishCallback publishCallback{};

    std::mutex lifecycleMutex{};
    std::condition_variable startupCondition{};
    bool startupResultReady{false};
    bool startupSucceeded{false};
    std::string startupError{};
    bool stopRequested{false};
    bool running{false};
    std::thread serverThread{};

    std::mutex legacyListenerMutex{};
    std::unordered_map<std::string, http_mqtt_interface_client_internal::LegacyListenerEndpoint> legacyListenerBySendToken{};
    std::unordered_map<std::string, std::string> receiveTokenBySendToken{};
    bool legacyDispatchStopRequested{false};
    std::thread legacyDispatchThread{};

    bool connectedClientsReportStopRequested{false};
    std::thread connectedClientsReportThread{};
};

} // namespace yaha
