#pragma once

/**
 * @file remote_service_http_adapter.h
 * @brief HTTP request adapter for RemoteService GET/POST contract.
 */

#include "yaha/remote_service/remote_service_component.h"

#include <functional>
#include <map>
#include <string>

namespace yaha {

inline constexpr int kDefaultRemoteServiceHttpStatusCode = 400;

/**
 * @brief Generic HTTP response envelope produced by RemoteService adapter.
 */
struct RemoteServiceHttpResponse {
    int statusCode{kDefaultRemoteServiceHttpStatusCode};       ///< HTTP status code.
    std::string contentType{"text/plain; charset=UTF-8"};    ///< Response content type.
    std::string payload{"Bad request"};                      ///< Response payload text.
};

/**
 * @brief Token validator callback signature used by request adapters.
 */
using RemoteServiceTokenValidator = std::function<bool(const std::string&)>;

/**
 * @brief HTTP adapter that maps GET/POST requests to RemoteService domain commands.
 */
class RemoteServiceHttpAdapter {
public:
    /**
     * @brief Constructs adapter bound to one RemoteService component.
     * @param component RemoteService domain component.
     */
    explicit RemoteServiceHttpAdapter(RemoteServiceComponent& component);

    /**
     * @brief Sets validator for GET `accessToken` requests.
     * @param validator Validator callback.
     */
    void setAccessTokenValidator(RemoteServiceTokenValidator validator);

    /**
     * @brief Sets validator for POST `deviceToken` requests.
     * @param validator Validator callback.
     */
    void setDeviceTokenValidator(RemoteServiceTokenValidator validator);

    /**
     * @brief Handles one GET request represented by decoded query map.
     * @param servicePath Exact request path.
     * @param queryValues Decoded query values.
     * @return HTTP response envelope.
     */
    [[nodiscard]] RemoteServiceHttpResponse handleGet(
        const std::string& servicePath,
        const std::map<std::string, std::string>& queryValues) const;

    /**
     * @brief Handles one POST request represented by JSON payload.
     * @param servicePath Exact request path.
     * @param payloadText JSON request payload.
     * @return HTTP response envelope.
     */
    [[nodiscard]] RemoteServiceHttpResponse handlePost(
        const std::string& servicePath,
        const std::string& payloadText) const;

private:
    [[nodiscard]] static RemoteServiceHttpResponse makeOkResponse();
    [[nodiscard]] static RemoteServiceHttpResponse makeBadRequestResponse();
    [[nodiscard]] static RemoteServiceHttpResponse makeNotFoundResponse();

    [[nodiscard]] RemoteServiceHttpResponse publishResolvedRequest(
        const RemoteServiceCommandRequest& requestData) const;

    RemoteServiceComponent& component_;
    RemoteServiceTokenValidator accessTokenValidator_{};
    RemoteServiceTokenValidator deviceTokenValidator_{};
};

} // namespace yaha