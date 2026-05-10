# remote_service_http — YAHA RemoteService HTTP Request Adapter

## Purpose

Implements HTTP GET/POST request adaptation for RemoteService by validating
input/token contracts and delegating command execution to
`RemoteServiceComponent::publishCommand`.

## Public API

### Struct `RemoteServiceHttpResponse`

| Field | Type | Notes |
|------|------|-------|
| `statusCode` | `int` | HTTP status code (`200`, `400`, `404`) |
| `contentType` | `std::string` | Response content type (`text/plain; charset=UTF-8`) |
| `payload` | `std::string` | Response payload (`ok`, `Bad request`, `Service not found`) |

### Alias `RemoteServiceTokenValidator`

`RemoteServiceTokenValidator = std::function<bool(const std::string&)>`

Token validator callback used for request-mode specific token checks.

### Class `RemoteServiceHttpAdapter`

| Member | Signature | Notes |
|------|------|------|
| ctor | `explicit RemoteServiceHttpAdapter(RemoteServiceComponent&)` | Binds adapter to one domain component instance |
| `setAccessTokenValidator` | `void(RemoteServiceTokenValidator)` | Configures GET token validator for `accessToken` |
| `setDeviceTokenValidator` | `void(RemoteServiceTokenValidator)` | Configures POST token validator for `deviceToken` |
| `handleGet` | `RemoteServiceHttpResponse(const std::string&, const std::map<std::string, std::string>&) const` | GET input parsing and publish handoff |
| `handlePost` | `RemoteServiceHttpResponse(const std::string&, const std::string&) const` | POST JSON parsing and publish handoff |

## Behavior

### GET adapter contract

- Required query keys:
  - `deviceId`
  - `state`
  - `accessToken`
- Missing required keys or empty required token/device id return `400`.
- `accessToken` is validated via configured GET token validator.
- Invalid token returns `400`.
- On valid input, adapter calls `publishCommand` on component.

### POST adapter contract

- Required JSON object fields:
  - `deviceId` (string)
  - `state` (string or number; `true`/`false`/`null` accepted as string payload)
  - `deviceToken` (string)
- Malformed JSON or missing/invalid required fields return `400`.
- `deviceToken` is validated via configured POST token validator.
- Invalid token returns `400`.
- On valid input, adapter calls `publishCommand` on component.

### Domain-result to HTTP mapping

- Domain status `Success` maps to:
  - `200`, payload `ok`
- Domain statuses `ServiceNotFound` and `PublishFailed` map to:
  - `404`, payload `Service not found`
- Unknown/unexpected domain status maps to:
  - `400`, payload `Bad request`

### Path routing behavior

- Adapter forwards `servicePath` exactly as passed to domain component.
- Matching remains case-sensitive and exact through domain path lookup.

## Files

| File | Role |
|------|------|
| `remote_service_http_adapter.h` | Public request/response adapter API |
| `remote_service_http_adapter.cpp` | GET/POST parsing, token validation, and result mapping |