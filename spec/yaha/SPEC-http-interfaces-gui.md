# YAHA HTTP Interface Specification for New GUI Clients

Date: 2026-05-09
Status: Active
Audience: Frontend and GUI developers

## 1. Scope

This document specifies the currently used YAHA HTTP interfaces for GUI integrations.

Included interfaces:
- FileStore: `GET` and `POST`
- MessageStore: `GET` and direct `POST` to `/store`
- HttpMqttInterface: `POST /publish`

Excluded by design:
- Backward-compatible PHP endpoints (for example `sensor.php`, `publish.php`) are not part of this contract.
- Legacy compatibility behavior is only mentioned when it directly affects the non-PHP endpoint behavior.

## 2. Common Conventions

### 2.1 Base URLs
Use deployment-specific host and port values:
- FileStore base: `http://<host>:8210`
- MessageStore base: `http://<host>:8090`
- HttpMqttInterface base: `http://<host>:8092` (or reverse proxy route)

### 2.2 Content Types
- JSON requests should use `Content-Type: application/json`.
- JSON responses use `application/json`.
- Some error responses are plain text with a structured YAHA error string.

### 2.3 Error Payload Format (plain text)
Some endpoints return text in this format:
- without details: `code=<CODE> | message=<TECHNICAL> | user_message=<USER_MESSAGE>`
- with details: `code=<CODE> | message=<TECHNICAL> | user_message=<USER_MESSAGE> | details=<DETAILS>`

Clients should parse these as plain text unless explicitly configured otherwise.

## 3. FileStore API

### 3.1 Purpose
Stores and retrieves one value per key path.

### 3.2 Endpoints
- `POST /<keyPath>`
- `GET /<keyPath>`

Example key path:
- `/automation/rules`

### 3.3 POST /<keyPath>
Write or replace a value.

Request:
- Method: `POST`
- Path: any key path (for example `/automation/rules`)
- Header: `Content-Type`
- Body: payload to store

Behavior:
- If `Content-Type` is exactly `application/json`, payload is stored as JSON.
- Any other content type is stored as plain text.
- On success returns HTTP `200` with empty body.

Important GUI note:
- `application/json; charset=utf-8` is not treated as JSON by this service.
- Send exactly `application/json` if JSON semantics are required.

Responses:
- `200` success
- `400` key too long (`YAHA_FILE_STORE_KEY_TOO_LONG`)
- `400` invalid JSON payload (`YAHA_FILE_STORE_INVALID_JSON_PAYLOAD`)
- `500` persist failed (`YAHA_FILE_STORE_PERSIST_FAILED`)

### 3.4 GET /<keyPath>
Read current value for key path.

Request:
- Method: `GET`
- Path: same key path used for write

Response:
- `200` with JSON body
- If value was stored as JSON: raw JSON value is returned
- If value was stored as text: JSON string is returned

Examples:
- Stored JSON object -> response body: `{"enabled":true}`
- Stored text `abc` -> response body: `"abc"`

Error responses:
- `400` key too long (`YAHA_FILE_STORE_KEY_TOO_LONG`)
- `404` key not found (`YAHA_FILE_STORE_KEY_NOT_FOUND`)
- `500` read failed (`YAHA_FILE_STORE_READ_FAILED`)

### 3.5 CORS
FileStore supports `OPTIONS` preflight with:
- `Access-Control-Allow-Origin: *`
- `Access-Control-Allow-Methods: POST, GET, OPTIONS`
- `Access-Control-Allow-Headers: content-type`

## 4. MessageStore API

### 4.1 Purpose
Query current and historical topic state data from the message tree.

### 4.2 Base Path
- `/store`

### 4.3 Data Node Schema
Each response node has this shape:

```json
{
  "topic": "first/study/main/light/light on time",
  "value": "on",
  "time": "2026-05-06T06:26:50.887Z",
  "reason": [
    { "message": "request by browser", "timestamp": "2026-05-06T06:26:50.887Z" }
  ],
  "history": [
    {
      "time": "2026-05-06T06:10:10.123Z",
      "value": "off",
      "reason": [
        { "message": "scheduler", "timestamp": "2026-05-06T06:10:10.123Z" }
      ]
    }
  ]
}
```

Notes:
- `time` fields are ISO-8601 UTC with trailing `Z`.
- `history` is newest first.

### 4.4 GET /store/<topicPrefix>
Section query mode.

Request:
- Method: `GET`
- Path: `/store` or `/store/<topicPrefix>`
- Optional headers:
  - `levelamount` (integer, default `1`)
  - `history` (bool-like, default `false`)
  - `reason` (bool-like, default `true`)

Accepted bool-like values:
- true: `1`, `true`, `yes`, `on`
- false: `0`, `false`, `no`, `off`

Response:
- `200` with JSON array of nodes

Errors:
- `404` unknown path (`YAHA_MESSAGE_STORE_HTTP_NOT_FOUND`)
- `400` invalid percent encoding in URL path (`YAHA_MESSAGE_STORE_HTTP_INVALID_PERCENT_ENCODING`)

### 4.5 Direct POST /store
Direct JSON object request for GUI use (sensor-compatible structure on direct route).

Request:
- Method: `POST`
- Path: `/store`
- Header: `Content-Type: application/json`
- Body: JSON object

Supported body fields:
- `topic` string, optional, topic prefix for query
- `history` bool or string bool token
- `reason` bool or string bool token
- `levelAmount` integer or integer string
- `levelamount` same as alias
- `nodes` optional snapshot array for diff mode

Body example:

```json
{
  "topic": "first/bathroom/main/light/light on time",
  "history": true,
  "reason": true,
  "levelAmount": 7,
  "nodes": []
}
```

Behavior details:
- Leading `/` in `topic` is normalized away.
- If `nodes` is non-empty and valid, snapshot diff mode is used.
- If `nodes` is `[]` or `null`, section mode is used.
- If JSON cannot be parsed as valid direct POST shape, service falls back to default section query behavior.

Response for parsed direct POST body:
- `200`
- JSON object with payload wrapper:

```json
{
  "payload": [
    {
      "topic": "...",
      "value": "...",
      "time": "...",
      "reason": [],
      "history": []
    }
  ]
}
```

Response for non-parsed POST body fallback:
- `200`
- JSON array (same as GET mode)

### 4.6 Recommendation: GET vs POST for GUI

For GUI clients, `POST /store` is the preferred interface after initial load.

Why `POST` is preferred:
- It supports sending client snapshot state in `nodes`, so server can return only differences.
- It reduces network payload and client-side processing for frequent updates.
- It keeps query parameters (`topic`, `history`, `reason`, `levelAmount`, `nodes`) in one explicit JSON contract.

When `GET` is still useful:
- Initial full load when no client snapshot exists yet.
- Manual diagnostics and quick browser/curl checks.
- Simple read-only queries without diff requirements.

Recommended GUI strategy:
- Step 1: perform initial read with `GET /store/<topicPrefix>`.
- Step 2: continue with `POST /store` and `nodes` snapshot diff requests.
- Step 3: if client state becomes inconsistent or POST fails repeatedly, perform one full reload via GET, then resume POST diff mode.

## 5. HttpMqttInterface Publish API

### 5.1 Purpose
Publish one message into MQTT via HTTP for browser/GUI clients.

### 5.2 Endpoint
- `POST /publish`

### 5.3 Input Sources
The endpoint accepts input from:
- request fields (query/form)
- JSON body (fallback when `topic` was not provided as field)

Supported input fields:
- `topic` string, required
- `value` string or number
- `reason` array of objects `{ "message": string, "timestamp": string }`
- `qos` integer `0..2` (default `1`)
- `retain` bool token (`true/false` or `1/0`, default `false`)

Topic normalization:
- `%2F` or `%2f` inside topic is converted to `/`.

If `reason` is missing:
- service auto-adds one reason entry with message `Request by browser` and generated timestamp.

### 5.4 Request Example

```http
POST /publish HTTP/1.1
Host: <host>:8092
Content-Type: application/json

{
  "topic": "first/study/main/light/light on time/set",
  "value": "off",
  "reason": [
    { "message": "request by browser", "timestamp": "2026-05-06T06:26:50.887Z" }
  ],
  "qos": 1,
  "retain": false
}
```

### 5.5 Responses
Success (native mode):
- HTTP `204 No Content`
- empty body

Errors:
- `400` invalid JSON (`{"error":"invalid_json"}`)
- `400` missing topic (`{"error":"missing_topic"}`)
- `405` unsupported method/route (`{"error":"unsupported_method"}`)
- `500` internal/downstream failure (`{"error":"internal_failure"}`)

Compatibility mode note:
- Some deployments may enable legacy response mode and return `200` with a JSON string payload instead of native `204`.
- GUI clients should prefer native mode and still tolerate this `200` variant for compatibility.

## 6. GUI Integration Recommendations

- Use one typed client module per service (`FileStoreClient`, `MessageStoreClient`, `PublishClient`).
- Treat FileStore errors as plain text YAHA error format, not JSON.
- For MessageStore direct POST, always send a valid object and always consume `.payload` when present.
- For Publish, enforce client-side validation for:
  - non-empty topic
  - `qos` in range `0..2`
  - valid reason array shape when provided
- Implement robust response handling:
  - success for publish: accept `204` and compatibility `200`
  - parse `{"error":...}` JSON on publish errors
  - parse plain text YAHA errors for FileStore and MessageStore path errors

## 7. Minimal JavaScript Examples

### 7.1 FileStore write/read

```js
async function writeRules(baseUrl, keyPath, rulesObject) {
  const res = await fetch(`${baseUrl}${keyPath}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(rulesObject)
  });
  if (!res.ok) throw new Error(await res.text());
}

async function readRules(baseUrl, keyPath) {
  const res = await fetch(`${baseUrl}${keyPath}`);
  if (!res.ok) throw new Error(await res.text());
  return res.json();
}
```

### 7.2 MessageStore direct POST

```js
async function queryMessageStore(baseUrl, topic) {
  const res = await fetch(`${baseUrl}/store`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      topic,
      history: true,
      reason: true,
      levelAmount: 5,
      nodes: []
    })
  });

  if (!res.ok) throw new Error(await res.text());

  const body = await res.json();
  return Array.isArray(body) ? body : (body.payload || []);
}
```

### 7.3 Publish

```js
async function publish(baseUrl, topic, value) {
  const res = await fetch(`${baseUrl}/publish`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ topic, value, qos: 1, retain: false })
  });

  if (res.status === 204) return;
  if (res.status === 200) return; // compatibility mode

  const text = await res.text();
  throw new Error(text);
}
```
