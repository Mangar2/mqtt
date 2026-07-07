#include "yaha/remote_service_http/remote_service_http_adapter.h"

#include "json/json_value.h"

#include <optional>
#include <string>

namespace yaha {

namespace {

constexpr int kHttpStatusOk{200};
constexpr int kHttpStatusBadRequest{400};
constexpr int kHttpStatusNotFound{404};

std::optional<Value> parseStateFromJsonValue(const mqtt::json::JsonValue& stateValue) {
    if (stateValue.is_string()) {
        return Value{stateValue.as_string()};
    }

    if (stateValue.is_number()) {
        return Value{stateValue.as_number()};
    }

    if (stateValue.is_boolean()) {
        return Value{stateValue.as_boolean() ? std::string{"true"} : std::string{"false"}};
    }

    if (stateValue.is_null()) {
        return Value{std::string{"null"}};
    }

    return std::nullopt;
}

bool validateToken(
    const RemoteServiceTokenValidator& validator,
    const std::string& tokenValue) {
    if (!validator) {
        return false;
    }
    return validator(tokenValue);
}

} // namespace

RemoteServiceHttpAdapter::RemoteServiceHttpAdapter(RemoteServiceComponent& component)
    : component_(component) {
}

void RemoteServiceHttpAdapter::setAccessTokenValidator(RemoteServiceTokenValidator validator) {
    accessTokenValidator_ = std::move(validator);
}

void RemoteServiceHttpAdapter::setDeviceTokenValidator(RemoteServiceTokenValidator validator) {
    deviceTokenValidator_ = std::move(validator);
}

RemoteServiceHttpResponse RemoteServiceHttpAdapter::handleGet(
    const std::string& servicePath,
    const std::map<std::string, std::string>& queryValues) const {
    const auto deviceIdIterator = queryValues.find("deviceId");
    const auto stateIterator = queryValues.find("state");
    const auto accessTokenIterator = queryValues.find("accessToken");
    if (deviceIdIterator == queryValues.end()
        || stateIterator == queryValues.end()
        || accessTokenIterator == queryValues.end()) {
        return makeBadRequestResponse();
    }

    if (deviceIdIterator->second.empty() || accessTokenIterator->second.empty()) {
        return makeBadRequestResponse();
    }

    if (!validateToken(accessTokenValidator_, accessTokenIterator->second)) {
        return makeBadRequestResponse();
    }

    const RemoteServiceCommandRequest requestData{
        .path = servicePath,
        .deviceId = deviceIdIterator->second,
        .state = Value{stateIterator->second},
        .token = accessTokenIterator->second};

    return publishResolvedRequest(requestData);
}

RemoteServiceHttpResponse RemoteServiceHttpAdapter::handlePost(
    const std::string& servicePath,
    const std::string& payloadText) const {
    const auto parsedPayload = mqtt::json::JsonValue::try_parse(payloadText);
    if (!parsedPayload.has_value() || !parsedPayload->is_object()) {
        return makeBadRequestResponse();
    }

    if (!parsedPayload->contains("deviceId")
        || !parsedPayload->contains("state")
        || !parsedPayload->contains("deviceToken")) {
        return makeBadRequestResponse();
    }

    const mqtt::json::JsonValue& deviceIdValue = parsedPayload->at("deviceId");
    const mqtt::json::JsonValue& stateTokenValue = parsedPayload->at("state");
    const mqtt::json::JsonValue& deviceTokenValue = parsedPayload->at("deviceToken");

    if (!deviceIdValue.is_string() || !deviceTokenValue.is_string()) {
        return makeBadRequestResponse();
    }

    const std::string deviceId = deviceIdValue.as_string();
    const std::string deviceToken = deviceTokenValue.as_string();
    if (deviceId.empty() || deviceToken.empty()) {
        return makeBadRequestResponse();
    }

    if (!validateToken(deviceTokenValidator_, deviceToken)) {
        return makeBadRequestResponse();
    }

    const std::optional<Value> stateValue = parseStateFromJsonValue(stateTokenValue);
    if (!stateValue.has_value()) {
        return makeBadRequestResponse();
    }

    const RemoteServiceCommandRequest requestData{
        .path = servicePath,
        .deviceId = deviceId,
        .state = *stateValue,
        .token = deviceToken};

    return publishResolvedRequest(requestData);
}

RemoteServiceHttpResponse RemoteServiceHttpAdapter::makeOkResponse() {
    return {
        .statusCode = kHttpStatusOk,
        .contentType = "text/plain; charset=UTF-8",
        .payload = "ok"};
}

RemoteServiceHttpResponse RemoteServiceHttpAdapter::makeBadRequestResponse() {
    return {
        .statusCode = kHttpStatusBadRequest,
        .contentType = "text/plain; charset=UTF-8",
        .payload = "Bad request"};
}

RemoteServiceHttpResponse RemoteServiceHttpAdapter::makeNotFoundResponse() {
    return {
        .statusCode = kHttpStatusNotFound,
        .contentType = "text/plain; charset=UTF-8",
        .payload = "Service not found"};
}

RemoteServiceHttpResponse RemoteServiceHttpAdapter::publishResolvedRequest(
    const RemoteServiceCommandRequest& requestData) const {
    const RemoteServiceCommandResult result = component_.publishCommand(requestData);
    if (result.status == RemoteServiceCommandStatus::Success) {
        return makeOkResponse();
    }
    if (result.status == RemoteServiceCommandStatus::ServiceNotFound
        || result.status == RemoteServiceCommandStatus::PublishFailed) {
        return makeNotFoundResponse();
    }

    return makeBadRequestResponse();
}

} // namespace yaha