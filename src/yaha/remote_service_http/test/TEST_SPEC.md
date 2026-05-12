# remote_service_http tests

## Scope

Unit tests for RemoteService HTTP adapter phase 4:

- GET request parsing and token validation
- POST request parsing and token validation
- domain result to HTTP status mapping
- exact and case-sensitive service-path behavior through domain delegation

## Planned test cases

| Test case | Scenario | Input | Expected |
|-----------|----------|-------|----------|
| `remote_service_http_get_success_publishes_mapped_command` | GET success path | loaded mapping, valid query, valid access token | adapter returns `200 ok` and publish callback receives mapped message |
| `remote_service_http_post_success_publishes_mapped_command` | POST success path | loaded mapping, valid JSON payload, valid device token | adapter returns `200 ok` and publish callback receives mapped message |
| `remote_service_http_get_unknown_path_returns_not_found` | exact-path routing unknown path | loaded mapping and valid token but unmatched path | adapter returns `404 Service not found` |
| `remote_service_http_get_case_mismatch_returns_not_found` | case-sensitive path behavior | loaded mapping and valid token with mismatched path case | adapter returns `404 Service not found` |
| `remote_service_http_post_malformed_json_returns_bad_request` | invalid POST JSON parser path | malformed payload text | adapter returns `400 Bad request` |
| `remote_service_http_get_missing_token_returns_bad_request` | missing GET token | query without `accessToken` | adapter returns `400 Bad request` |
| `remote_service_http_get_empty_or_unvalidated_token_returns_bad_request` | GET validator and required-field guards | empty token/device id or missing validator callback | adapter returns `400 Bad request` |
| `remote_service_http_post_invalid_token_returns_bad_request` | invalid POST token path | valid payload with token rejected by validator | adapter returns `400 Bad request` |
| `remote_service_http_post_requires_string_identity_fields` | POST identity fields must be strings | payload with numeric `deviceId` or `deviceToken` | adapter returns `400 Bad request` |
| `remote_service_http_post_requires_all_required_fields` | POST required keys validation | payload missing one of `deviceId`, `state`, `deviceToken` | adapter returns `400 Bad request` |
| `remote_service_http_post_accepts_numeric_and_boolean_state_tokens` | non-string state token parsing | valid payload with numeric state and boolean state | adapter returns `200 ok` and publishes mapped values as `double` and `string("true")` |