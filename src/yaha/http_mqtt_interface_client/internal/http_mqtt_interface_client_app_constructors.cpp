#include "yaha/http_mqtt_interface_client/internal/http_mqtt_interface_client_app_internal.h"

#include "yaha/error_handling/yaha_error.h"
#include "yaha/mqtt_client/broker_transport.h"
#include "json/json_value.h"

#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace yaha {
using namespace http_mqtt_interface_client_internal;

HttpMqttInterfaceClientComponent::HttpMqttInterfaceClientComponent(HttpMqttInterfaceClientConfig configInput)
    : HttpMqttInterfaceClientComponent(
        std::move(configInput),
        []() {
            return makeBrokerTransport();
        }) {}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
HttpMqttInterfaceClientComponent::HttpMqttInterfaceClientComponent(
    HttpMqttInterfaceClientConfig configInput,
    HttpMqttSessionTransportFactory sessionTransportFactory)
    : impl_(std::make_unique<Impl>(std::move(configInput), std::move(sessionTransportFactory))) {
    impl_->server.Get(k_healthEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        response.status = k_httpStatusOk;
        response.set_content("ok", "text/plain");
    });

    impl_->server.Put(k_publishEndpoint.data(),
                      // NOLINTNEXTLINE(readability-function-cognitive-complexity)
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "publish", request, response)) {
                              return;
                          }

                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          const std::string token = resolveToken(request, fields, jsonBody);
                          const std::string clientId = resolveClientIdForRequest(impl_->sessionManager, jsonBody, token);
                          const std::string topic = resolveTopicForRequest(fields, jsonBody);
                          logIncomingPublishRequest(
                              impl_->config.logIncomingRequests,
                              request,
                              k_publishEndpoint,
                              clientId,
                              token,
                              topic);
                          try {
                              HttpMqttHeaders compatibilityFields = collectHeaders(request);
                              for (const auto& [fieldName, fieldValue] : fields) {
                                  compatibilityFields[fieldName] = fieldValue;
                              }

                              const HttpMqttPublishCompatibilityRequest compatibilityRequest{
                                  .method = request.method,
                                  .endpoint = std::string{k_publishEndpoint},
                                  .headers = collectHeaders(request),
                                  .fields = compatibilityFields,
                                  .body = request.body,
                                  .token = token,
                              };

                              const HttpMqttResult compatibilityResult = handlePublishCompatibilityRequest(
                                  impl_->interfaces,
                                  compatibilityRequest,
                                  impl_->compatibilityConfig,
                                  [this](const HttpMqttRequestData& downstreamRequest, const Message& mappedMessage) {
                                      std::string sessionToken{};
                                      if (const auto parsedPayload = mqtt::json::JsonValue::try_parse(downstreamRequest.payload);
                                          parsedPayload.has_value()) {
                                          if (const auto tokenField = tryReadStringField(*parsedPayload, "token"); tokenField.has_value()) {
                                              sessionToken = *tokenField;
                                          }
                                      }

                                      const bool useManagedSessionPath =
                                          !sessionToken.empty() && impl_->sessionManager.hasSession(sessionToken);
                                      const std::string_view dispatchPath = useManagedSessionPath
                                          ? std::string_view{"managed_session"}
                                          : std::string_view{"callback_listener"};
                                      logBrokerPublishDispatchAttempt(
                                          impl_->config.logTracing,
                                          mappedMessage,
                                          sessionToken,
                                          dispatchPath);

                                      if (useManagedSessionPath) {
                                          std::string sessionError{};
                                          if (!impl_->sessionManager.publish(sessionToken, mappedMessage, sessionError)) {
                                              const std::string publishReason = sessionError.empty()
                                                  ? "broker publish callback failed"
                                                  : sessionError;
                                              logBrokerPublishDispatchFailed(
                                                  impl_->config.logErrors,
                                                  mappedMessage,
                                                  sessionToken,
                                                  dispatchPath,
                                                  publishReason);
                                              logBrokerForwardPublishError(
                                                  impl_->config.logBrokerMessages,
                                                  impl_->config.logReason,
                                                  mappedMessage,
                                                  publishReason);
                                              throw YahaError{
                                                  k_error_code_broker_publish_failed,
                                                  publishReason,
                                                  "broker publish failed",
                                              };
                                          }

                                      } else {
                                          PublishResult publishResult{};
                                          {
                                              std::lock_guard<std::mutex> lock{impl_->publishCallbackMutex};
                                              publishResult = impl_->publishCallback(mappedMessage);
                                          }

                                          if (!publishResult.success) {
                                              const std::string publishReason = publishResult.reason.empty()
                                                  ? "broker publish callback failed"
                                                  : publishResult.reason;
                                              logBrokerPublishDispatchFailed(
                                                  impl_->config.logErrors,
                                                  mappedMessage,
                                                  sessionToken,
                                                  dispatchPath,
                                                  publishReason);
                                              logBrokerForwardPublishError(
                                                  impl_->config.logBrokerMessages,
                                                  impl_->config.logReason,
                                                  mappedMessage,
                                                  publishReason);
                                              throw YahaError{
                                                  k_error_code_broker_publish_failed,
                                                  publishReason,
                                                  "broker publish failed",
                                              };
                                          }
                                      }

                                      logBrokerPublishDispatchSent(
                                          impl_->config.logTracing,
                                          mappedMessage,
                                          sessionToken,
                                          dispatchPath);
                                      logBrokerForwardPublishAck(
                                          impl_->config.logBrokerMessages,
                                          impl_->config.logReason,
                                          mappedMessage);
                                      return impl_->interfaces.onPublish(downstreamRequest.headers);
                                  });

                              if (compatibilityResult.statusCode >= k_httpStatusInternalServerError) {
                                  logCompatibilityInternalResultFailure(
                                      impl_->config.logErrors,
                                      k_publishEndpoint,
                                      compatibilityResult);
                              }
                              applyHttpMqttResult(compatibilityResult, response);
                          } catch (const std::exception& exceptionValue) {
                              logCompatibilityRequestFailure(
                                  impl_->config.logErrors,
                                  k_publishEndpoint,
                                  withRawBodyDetail(exceptionValue.what(), request));
                              applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                          } catch (...) {
                              logCompatibilityRequestFailure(
                                  impl_->config.logErrors,
                                  k_publishEndpoint,
                                  withRawBodyDetail("unknown publish request error", request));
                              applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                          }
                      });

    impl_->server.Put(k_pubrelEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "pubrel", request, response)) {
                              return;
                          }

                          try {
                              applyHttpMqttResult(impl_->interfaces.onPubrel(collectHeaders(request)), response);
                          } catch (const std::exception& exceptionValue) {
                              logCompatibilityRequestFailure(
                                  impl_->config.logErrors,
                                  k_pubrelEndpoint,
                                  withRawBodyDetail(exceptionValue.what(), request));
                              applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                          } catch (...) {
                              logCompatibilityRequestFailure(
                                  impl_->config.logErrors,
                                  k_pubrelEndpoint,
                                  withRawBodyDetail("unknown publish request error", request));
                              applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
                          }
                      });

    impl_->server.Put(k_connectEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "connect", request, response)) {
                              return;
                          }

                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "connect",
                                  "invalid json payload",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          const auto clientId = jsonBody.has_value()
                              ? tryReadStringField(*jsonBody, "clientId")
                              : std::nullopt;
                          if (!clientId.has_value() || clientId->empty()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "connect",
                                  "missing clientId",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_client_id"), response);
                              return;
                          }

                          HttpMqttSessionConnectRequest connectRequest{};
                          connectRequest.clientId = *clientId;
                          if (jsonBody.has_value()) {
                              // Compatibility rule:
                              // - legacy TypeScript connect payload uses host/port for client callback listener.
                              // - broker target overrides are accepted only via explicit brokerHost/brokerPort.
                              connectRequest.brokerHost = tryReadStringField(*jsonBody, "brokerHost");
                              connectRequest.brokerPort = tryReadUInt16Field(*jsonBody, "brokerPort");

                              const auto keepAliveSeconds = tryReadUInt32Field(*jsonBody, "keepAliveSeconds");
                              if (keepAliveSeconds.has_value()) {
                                  connectRequest.keepAliveSeconds = keepAliveSeconds;
                              } else {
                                  connectRequest.keepAliveSeconds = tryReadUInt32Field(*jsonBody, "keepAlive");
                              }
                          }

                          HttpMqttSessionConnectTokens tokens{};
                          std::string connectError{};
                          if (!impl_->sessionManager.connect(connectRequest, tokens, connectError)) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "connect",
                                  "session connect failed",
                                  withRawBodyDetail(connectError, request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "connect_failed"), response);
                              return;
                          }

                          if (jsonBody.has_value()) {
                              const auto legacyListenerHost = tryReadStringField(*jsonBody, "host");
                              const auto legacyListenerPort = tryReadUInt16Field(*jsonBody, "port");
                              if (legacyListenerHost.has_value() && !legacyListenerHost->empty() && legacyListenerPort.has_value()) {
                                  std::lock_guard<std::mutex> lock{impl_->legacyListenerMutex};
                                  impl_->legacyListenerBySendToken[tokens.sendToken] = LegacyListenerEndpoint{
                                      .host = *legacyListenerHost,
                                      .port = *legacyListenerPort,
                                  };
                                  impl_->receiveTokenBySendToken[tokens.sendToken] = tokens.receiveToken;
                              }
                          }

                          HttpMqttConnectResult connectResult{};
                          connectResult.present = 0U;
                          connectResult.token.send = tokens.sendToken;
                          connectResult.token.receive = tokens.receiveToken;

                          HttpMqttHeaders headers = collectHeaders(request);
                          if (headers.find("version") == headers.end()) {
                              headers["version"] = "1.0";
                          }

                          try {
                              applyHttpMqttResult(impl_->interfaces.onConnect(headers, connectResult), response);
                              logHttpMqttEvent(
                                  impl_->config.logEvents,
                                  "connect",
                                  std::string{"clientId="} + *clientId +
                                      " sendToken=" + tokens.sendToken +
                                      " receiveToken=" + tokens.receiveToken);
                          } catch (...) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "connect",
                                  "connect response mapping failed",
                                  withRawBodyDetail("request_processing_failed", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "connect_response_failed"), response);
                          }
                      });

    impl_->server.Put(k_subscribeEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "subscribe", request, response)) {
                              return;
                          }

                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "subscribe",
                                  "invalid json payload",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          std::string topicsError{};
                          const std::map<std::string, Qos> topics = parseTopicsObject(jsonBody, topicsError, "subscribe");
                          if (!topicsError.empty()) {
                              const std::string token = resolveToken(request, fields, jsonBody);
                              const std::string clientId = resolveClientIdForRequest(impl_->sessionManager, jsonBody, token);
                              std::string detailText = std::string{k_subscribe_topics_shape_error_prefix} + topicsError;
                              if (!clientId.empty()) {
                                  detailText += " clientId=" + clientId;
                              }
                              if (!token.empty()) {
                                  detailText += " token=" + token;
                              }
                              detailText += " raw_body=" + request.body;
                              logHttpMqttError(impl_->config.logErrors, "subscribe", "invalid topics payload", detailText);
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_topics"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          std::string resolvedToken = token;
                          if (resolvedToken.empty() && jsonBody.has_value()) {
                              if (const auto clientId = tryReadStringField(*jsonBody, "clientId"); clientId.has_value()) {
                                  const bool clientResolved = impl_->sessionManager.resolveSendTokenByClientId(
                                      *clientId,
                                      resolvedToken);
                                  (void)clientResolved;
                              }
                          }
                          if (resolvedToken.empty()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "subscribe",
                                  "missing token",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_token"), response);
                              return;
                          }

                          HttpMqttHeaders headers = collectHeaders(request);
                          if (headers.find("version") == headers.end()) {
                              headers["version"] = "1.0";
                          }
                          if (jsonBody.has_value()) {
                              if (const auto packetId = tryReadIntField(*jsonBody, "packetid"); packetId.has_value()) {
                                  headers["packetid"] = std::to_string(*packetId);
                              }
                          }

                          std::vector<std::uint8_t> subscribeResult{};
                          std::string subscribeError{};
                          if (!impl_->sessionManager.subscribe(resolvedToken, topics, subscribeResult, subscribeError)) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "subscribe",
                                  "session subscribe failed",
                                  withRawBodyDetail(subscribeError, request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "subscribe_failed"), response);
                              return;
                          }

                          std::string resolvedClientId{};
                          (void)impl_->sessionManager.resolveClientIdByToken(resolvedToken, resolvedClientId);

                          try {
                              applyHttpMqttResult(impl_->interfaces.onSubscribe(headers, subscribeResult), response);
                              std::ostringstream detailText{};
                              if (!resolvedClientId.empty()) {
                                  detailText << "clientId=" << resolvedClientId << ' ';
                              }
                              detailText << "token=" << resolvedToken
                                         << " topics_count=" << topics.size();
                              logHttpMqttEvent(impl_->config.logEvents, "subscribe", detailText.str());
                          } catch (...) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "subscribe",
                                  "subscribe response mapping failed",
                                  withRawBodyDetail("request_processing_failed", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "subscribe_response_failed"), response);
                          }
                      });

    impl_->server.Put(k_unsubscribeEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "unsubscribe", request, response)) {
                              return;
                          }

                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "unsubscribe",
                                  "invalid json payload",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          std::string topicsError{};
                          const std::map<std::string, Qos> topics = parseTopicsObject(jsonBody, topicsError, "unsubscribe");
                          if (!topicsError.empty()) {
                              const std::string token = resolveToken(request, fields, jsonBody);
                              const std::string clientId = resolveClientIdForRequest(impl_->sessionManager, jsonBody, token);
                              std::string detailText = std::string{k_unsubscribe_topics_shape_error_prefix} + topicsError;
                              if (!clientId.empty()) {
                                  detailText += " clientId=" + clientId;
                              }
                              if (!token.empty()) {
                                  detailText += " token=" + token;
                              }
                              detailText += " raw_body=" + request.body;
                              logHttpMqttError(impl_->config.logErrors, "unsubscribe", "invalid topics payload", detailText);
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_topics"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          std::string resolvedToken = token;
                          if (resolvedToken.empty() && jsonBody.has_value()) {
                              if (const auto clientId = tryReadStringField(*jsonBody, "clientId"); clientId.has_value()) {
                                  const bool clientResolved = impl_->sessionManager.resolveSendTokenByClientId(
                                      *clientId,
                                      resolvedToken);
                                  (void)clientResolved;
                              }
                          }
                          if (resolvedToken.empty()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "unsubscribe",
                                  "missing token",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_token"), response);
                              return;
                          }

                          HttpMqttHeaders headers = collectHeaders(request);
                          if (headers.find("version") == headers.end()) {
                              headers["version"] = "1.0";
                          }
                          if (jsonBody.has_value()) {
                              if (const auto packetId = tryReadIntField(*jsonBody, "packetid"); packetId.has_value()) {
                                  headers["packetid"] = std::to_string(*packetId);
                              }
                          }

                          std::vector<std::uint8_t> unsubscribeResult{};
                          std::string unsubscribeError{};
                          if (!impl_->sessionManager.unsubscribe(resolvedToken, topics, unsubscribeResult, unsubscribeError)) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "unsubscribe",
                                  "session unsubscribe failed",
                                  withRawBodyDetail(unsubscribeError, request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "unsubscribe_failed"), response);
                              return;
                          }

                          std::string resolvedClientId{};
                          (void)impl_->sessionManager.resolveClientIdByToken(resolvedToken, resolvedClientId);

                          try {
                              applyHttpMqttResult(impl_->interfaces.onUnsubscribe(headers, unsubscribeResult), response);
                              std::ostringstream detailText{};
                              if (!resolvedClientId.empty()) {
                                  detailText << "clientId=" << resolvedClientId << ' ';
                              }
                              detailText << "token=" << resolvedToken
                                         << " topics_count=" << topics.size();
                              logHttpMqttEvent(impl_->config.logEvents, "unsubscribe", detailText.str());
                          } catch (...) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "unsubscribe",
                                  "unsubscribe response mapping failed",
                                  withRawBodyDetail("request_processing_failed", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "unsubscribe_response_failed"), response);
                          }
                      });

    impl_->server.Put(k_disconnectEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "disconnect", request, response)) {
                              return;
                          }

                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "disconnect",
                                  "invalid json payload",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          std::string resolvedToken = token;
                          if (resolvedToken.empty() && jsonBody.has_value()) {
                              if (const auto clientId = tryReadStringField(*jsonBody, "clientId"); clientId.has_value()) {
                                  const bool clientResolved = impl_->sessionManager.resolveSendTokenByClientId(
                                      *clientId,
                                      resolvedToken);
                                  (void)clientResolved;
                              }
                          }
                          if (resolvedToken.empty()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "disconnect",
                                  "missing token",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_token"), response);
                              return;
                          }

                          std::string disconnectError{};
                          std::string disconnectClientId{};
                          (void)impl_->sessionManager.resolveClientIdByToken(resolvedToken, disconnectClientId);
                          if (!impl_->sessionManager.disconnect(resolvedToken, disconnectError)) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "disconnect",
                                  "session disconnect failed",
                                  withRawBodyDetail(disconnectError, request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "disconnect_failed"), response);
                              return;
                          }

                          {
                              std::lock_guard<std::mutex> lock{impl_->legacyListenerMutex};
                              impl_->legacyListenerBySendToken.erase(resolvedToken);
                              impl_->receiveTokenBySendToken.erase(resolvedToken);
                          }

                          HttpMqttHeaders headers = collectHeaders(request);
                          if (headers.find("version") == headers.end()) {
                              headers["version"] = "1.0";
                          }

                          try {
                              applyHttpMqttResult(impl_->interfaces.onDisconnect(headers), response);
                              std::string detailText{"token=" + resolvedToken};
                              if (!disconnectClientId.empty()) {
                                  detailText += " clientId=" + disconnectClientId;
                              }
                              logHttpMqttEvent(impl_->config.logEvents, "disconnect", detailText);
                          } catch (...) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "disconnect",
                                  "disconnect response mapping failed",
                                  withRawBodyDetail("request_processing_failed", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "disconnect_response_failed"), response);
                          }
                      });
    impl_->server.Put(k_pingEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "ping", request, response)) {
                              return;
                          }

                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "ping",
                                  "invalid json payload",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          if (token.empty()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "ping",
                                  "missing token",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_token"), response);
                              return;
                          }

                          std::string resolvedClientId{};
                          (void)impl_->sessionManager.resolveClientIdByToken(token, resolvedClientId);

                          std::string pingError{};
                          if (!impl_->sessionManager.ping(token, pingError)) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "ping",
                                  "session ping failed",
                                  withRawBodyDetail(pingError, request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "ping_failed"), response);
                              return;
                          }

                          applyHttpMqttResult(makeNoContentResult("pingresp"), response);
                          std::ostringstream detailText{};
                          if (!resolvedClientId.empty()) {
                              detailText << "clientId=" << resolvedClientId << ' ';
                          }
                          detailText << "token=" << token;
                          logHttpMqttEvent(impl_->config.logEvents, "ping", detailText.str());
                      });

    impl_->server.Put(k_receiveEndpoint.data(),
                      [this](const httplib::Request& request, httplib::Response& response) {
                          if (!ensureSupportedRequestVersion(impl_->config.logErrors, "receive", request, response)) {
                              return;
                          }

                          const HttpMqttHeaders fields = collectFields(request);
                          const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
                          if (!request.body.empty() && !jsonBody.has_value()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "receive",
                                  "invalid json payload",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "invalid_json"), response);
                              return;
                          }

                          const std::string token = resolveToken(request, fields, jsonBody);
                          if (token.empty()) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "receive",
                                  "missing token",
                                  withRawBodyDetail("request_invalid", request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusBadRequest, "missing_token"), response);
                              return;
                          }

                          std::optional<Message> receivedMessage{};
                          std::string receiveError{};
                          if (!impl_->sessionManager.receive(token, receivedMessage, receiveError)) {
                              logHttpMqttError(
                                  impl_->config.logErrors,
                                  "receive",
                                  "session receive failed",
                                  withRawBodyDetail(receiveError, request));
                              applyHttpMqttResult(makeJsonErrorResult(k_httpStatusInternalServerError, "receive_failed"), response);
                              return;
                          }

                          if (!receivedMessage.has_value()) {
                              applyHttpMqttResult(makeNoContentResult(), response);
                              return;
                          }

                          HttpMqttResult receiveResult{};
                          receiveResult.statusCode = k_httpStatusOk;
                          receiveResult.headers["content-type"] = "application/json; charset=UTF-8";
                          receiveResult.headers["version"] = "1.0";
                          receiveResult.headers["packet"] = "publish";
                          receiveResult.payload = buildReceivePayload(*receivedMessage);
                          applyHttpMqttResult(receiveResult, response);
                          logBrokerIncomingMessage(
                              impl_->config.logBrokerMessages,
                              impl_->config.logReason,
                              *receivedMessage);
                      });

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    const auto handleCompatibilityRequest = [this](
                                                const httplib::Request& request,
                                                httplib::Response& response,
                                                const std::string_view endpoint) {
        try {
            if (!ensureSupportedRequestVersion(impl_->config.logErrors, "publish_compatibility", request, response)) {
                return;
            }

            const HttpMqttHeaders headers = collectHeaders(request);
            const HttpMqttHeaders fields = collectFields(request);
            const std::optional<mqtt::json::JsonValue> jsonBody = tryParseJsonBody(request);
            const std::string token = resolveToken(request, fields, jsonBody);
            const std::string clientId = resolveClientIdForRequest(impl_->sessionManager, jsonBody, token);
            const std::string topic = resolveTopicForRequest(fields, jsonBody);
            logIncomingPublishRequest(
                impl_->config.logIncomingRequests,
                request,
                endpoint,
                clientId,
                token,
                topic);
            HttpMqttHeaders compatibilityFields = headers;
            for (const auto& [fieldName, fieldValue] : fields) {
                compatibilityFields[fieldName] = fieldValue;
            }
            const HttpMqttPublishCompatibilityRequest compatibilityRequest{
                .method = request.method,
                .endpoint = std::string{endpoint},
                .headers = headers,
                .fields = compatibilityFields,
                .body = request.body,
                .token = token,
            };

            const HttpMqttResult compatibilityResult = handlePublishCompatibilityRequest(
                impl_->interfaces,
                compatibilityRequest,
                impl_->compatibilityConfig,
                [this](const HttpMqttRequestData& downstreamRequest, const Message& mappedMessage) {
                    std::string sessionToken{};
                    if (const auto parsedPayload = mqtt::json::JsonValue::try_parse(downstreamRequest.payload);
                        parsedPayload.has_value()) {
                        if (const auto tokenField = tryReadStringField(*parsedPayload, "token"); tokenField.has_value()) {
                            sessionToken = *tokenField;
                        }
                    }

                    const bool useManagedSessionPath =
                        !sessionToken.empty() && impl_->sessionManager.hasSession(sessionToken);
                    const std::string_view dispatchPath = useManagedSessionPath
                        ? std::string_view{"managed_session"}
                        : std::string_view{"callback_listener"};
                    logBrokerPublishDispatchAttempt(
                        impl_->config.logTracing,
                        mappedMessage,
                        sessionToken,
                        dispatchPath);

                    if (useManagedSessionPath) {
                        std::string sessionError{};
                        if (!impl_->sessionManager.publish(sessionToken, mappedMessage, sessionError)) {
                            const std::string publishReason = sessionError.empty()
                                ? "broker publish callback failed"
                                : sessionError;
                            logBrokerPublishDispatchFailed(
                                impl_->config.logErrors,
                                mappedMessage,
                                sessionToken,
                                dispatchPath,
                                publishReason);
                            logBrokerForwardPublishError(
                                impl_->config.logBrokerMessages,
                                impl_->config.logReason,
                                mappedMessage,
                                publishReason);
                            throw YahaError{
                                k_error_code_broker_publish_failed,
                                publishReason,
                                "broker publish failed",
                            };
                        }

                    } else {
                        PublishResult publishResult{};
                        {
                            std::lock_guard<std::mutex> lock{impl_->publishCallbackMutex};
                            publishResult = impl_->publishCallback(mappedMessage);
                        }

                        if (!publishResult.success) {
                            const std::string publishReason = publishResult.reason.empty()
                                ? "broker publish callback failed"
                                : publishResult.reason;
                            logBrokerPublishDispatchFailed(
                                impl_->config.logErrors,
                                mappedMessage,
                                sessionToken,
                                dispatchPath,
                                publishReason);
                            logBrokerForwardPublishError(
                                impl_->config.logBrokerMessages,
                                impl_->config.logReason,
                                mappedMessage,
                                publishReason);
                            throw YahaError{
                                k_error_code_broker_publish_failed,
                                publishReason,
                                "broker publish failed",
                            };
                        }
                    }

                    logBrokerPublishDispatchSent(
                        impl_->config.logTracing,
                        mappedMessage,
                        sessionToken,
                        dispatchPath);
                    logBrokerForwardPublishAck(
                        impl_->config.logBrokerMessages,
                        impl_->config.logReason,
                        mappedMessage);
                    return impl_->interfaces.onPublish(downstreamRequest.headers);
                });

            if (compatibilityResult.statusCode >= k_httpStatusInternalServerError) {
                logCompatibilityInternalResultFailure(impl_->config.logErrors, endpoint, compatibilityResult);
            }
            applyHttpMqttResult(compatibilityResult, response);
        } catch (const std::exception& exceptionValue) {
            logCompatibilityRequestFailure(
                impl_->config.logErrors,
                endpoint,
                withRawBodyDetail(exceptionValue.what(), request));
            applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
        } catch (...) {
            logCompatibilityRequestFailure(
                impl_->config.logErrors,
                endpoint,
                withRawBodyDetail("unknown publish request error", request));
            applyHttpMqttResult(makeCompatibilityInternalErrorResult(), response);
        }
    };

    impl_->server.Post(k_publishEndpoint.data(),
                       [handleCompatibilityRequest](const httplib::Request& request, httplib::Response& response) {
                           handleCompatibilityRequest(request, response, k_publishEndpoint);
                       });

    impl_->server.Post(k_publishPhpEndpoint.data(),
                       [handleCompatibilityRequest](const httplib::Request& request, httplib::Response& response) {
                           handleCompatibilityRequest(request, response, k_publishPhpEndpoint);
                       });

    impl_->server.Options(k_publishEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_publishPhpEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_pubrelEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_connectEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_disconnectEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_subscribeEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_unsubscribeEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_pingEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

    impl_->server.Options(k_receiveEndpoint.data(), [](const httplib::Request&, httplib::Response& response) {
        applyHttpMqttCorsHeaders(response, true);
        response.status = k_httpStatusNoContent;
        response.set_content("", "text/plain");
    });

}

} // namespace yaha
