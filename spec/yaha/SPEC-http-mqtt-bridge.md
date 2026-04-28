# HTTP-MQTT Bridge

## Purpose

Allows simple devices (microcontrollers, embedded systems) that cannot implement the full MQTT wire protocol to participate in the YAHA message system using plain HTTP. The bridge translates between a lightweight HTTP-based protocol and real MQTT, acting as a thin proxy between devices and yahabroker.

## Why a separate executable, not a broker feature

The bridge must not be built into yahabroker. Reasons:

- yahabroker speaks the MQTT wire protocol. Adding an HTTP interface mixes protocol responsibilities and increases broker complexity and attack surface.
- The bridge connects to yahabroker as a regular MQTT client. yahabroker has no knowledge of the bridge's existence.
- The bridge can be deployed, restarted, and scaled independently. If it crashes, yahabroker and all other clients continue unaffected.

## Design principle: maximum delegation to the broker

The bridge is intentionally thin. It delegates all session state, QoS handling, message queuing, and reconnect management entirely to yahabroker by opening **one real MQTT connection per device**. The bridge itself holds no queues, no session state beyond the in-memory device registry, and no persistence.

When the bridge restarts, all device MQTT connections are gone. The device is responsible for re-calling `/connect` when it detects its connection is lost.

## Role in the system

```
[Device]  ←→  HTTP  ←→  [HTTP-MQTT Bridge]  ←→  MQTT (per device conn.)  ←→  [yahabroker]
                               ↕ HTTP PUT
                          [Device callbacks]
```

The bridge has two communication roles:
1. **HTTP server**: receives commands from devices (connect, publish, subscribe, unsubscribe, disconnect).
2. **One MQTT client per device**: each device gets its own real MQTT connection to yahabroker. All MQTT concerns (QoS, session, will, reconnect, keep-alive) are handled by the broker.

For incoming messages (broker → device): when a message arrives on a device's MQTT connection, the bridge delivers it to the device via HTTP PUT to the device's registered callback address.

## Standalone program structure

The bridge is a standalone executable. Its entry point creates:
- An HTTP server that accepts device commands.
- An HTTP client for delivering messages to device callbacks.
- A per-device MQTT connection factory: each `/connect` call creates a new MQTT client instance.

No `IMqttComponent` interface is needed: the bridge is the MQTT client logic. Both translation sides are its sole purpose.

## In-memory device registry

The bridge keeps one entry per connected device:

| Field    | Meaning                                         |
|----------|-------------------------------------------------|
| clientId | Unique device identifier                        |
| host     | Device callback hostname or IP                  |
| port     | Device callback port                            |
| mqtt     | The live MQTT client instance for this device   |

This registry exists only in memory. It is lost on bridge restart.

## HTTP interface (device → bridge)

All endpoints use HTTP PUT with a JSON body. Headers carry MQTT metadata.

### PUT /connect

Registers a device. The bridge opens a new MQTT connection to yahabroker on behalf of the device.

**Body:**

| Field     | Type    | Required | Meaning                                                    |
|-----------|---------|----------|------------------------------------------------------------|
| clientId  | string  | yes      | Unique identifier for this device                          |
| host      | string  | yes      | Hostname or IP the bridge will call back for delivery      |
| port      | integer | yes      | Port on the device for callback delivery                   |
| clean     | boolean | yes      | Passed through to MQTT CONNECT; broker manages session     |
| keepAlive | integer | no       | Passed through to MQTT CONNECT                             |
| user      | string  | no       | Passed through to MQTT CONNECT                             |
| password  | string  | no       | Passed through to MQTT CONNECT                             |
| will      | Message | no       | Passed through to MQTT CONNECT                             |

**Response:** 200 on success.

### PUT /publish

Publishes a message via the device's MQTT connection.

**Body:** a Message object (see [SPEC-message.md](./SPEC-message.md)).

**Headers:** `qos`, `retain`, `dup`, `packetid`, `version`.

**Response:** 200 on acceptance. QoS acknowledgement is handled by the MQTT layer; the bridge returns immediately after handing the message to the MQTT client.

### PUT /subscribe

Subscribes via the device's MQTT connection.

**Body:** map of topic pattern → QoS level.

**Response:** 200 with granted QoS per topic.

### PUT /unsubscribe

Unsubscribes via the device's MQTT connection.

**Body:** list of topic patterns.

### PUT /disconnect

Closes the device's MQTT connection and removes the device from the registry.

## HTTP callback interface (bridge → device)

When a message arrives on a device's MQTT connection, the bridge delivers it:

### PUT `http://<device-host>:<device-port>/publish`

**Body:** a Message object.

**Headers:** `qos`, `dup`, `packetid`, `version`.

**On failure:** the bridge makes a configurable number of retries. If all retries fail, the device entry is removed from the registry and its MQTT connection is closed.

## Configuration

| Parameter                              | Meaning                                               |
|----------------------------------------|-------------------------------------------------------|
| port                                   | HTTP server port the bridge listens on                |
| cors.allowOrigin                       | List of allowed CORS origins (supports `*`)           |
| broker.host                            | Hostname of yahabroker                                |
| broker.port                            | MQTT port of yahabroker                               |
| delivery.replyTimeoutInMilliseconds    | Timeout for HTTP callback delivery to device          |
| delivery.maxRetryCount                 | Retries before removing unreachable device            |

## Error handling

- Unknown HTTP path: 404.
- Malformed JSON body: 400, no state change.
- `/publish`, `/subscribe`, `/unsubscribe`, `/disconnect` for unknown clientId: 404.
- Device callback unreachable after all retries: device removed from registry, MQTT connection closed.
- Bridge loses MQTT connection for a device: the MQTT client reconnects automatically (keep-alive / ping mechanism). Device is not notified; it will eventually see missing callbacks and reconnect.

## CORS

The bridge supports CORS for browser-based clients. `Access-Control-Allow-Origin` is set from the configured allowed origins. OPTIONS pre-flight requests return allowed methods (`OPTIONS, GET, PUT`).

## Architectural notes

- One real MQTT connection per device. The broker owns all session state, QoS guarantees, and message queuing.
- The bridge holds no persistent state. Restart = all devices must reconnect.
- The MQTT client per device manages its own reconnect loop internally (ping-based keep-alive). The bridge does not implement reconnect logic.
- TLS termination and external access control are handled by nginx in front of the bridge's HTTP server.
- No WebSocket support. HTTP only between bridge and devices.
- Failed callback delivery is handled silently: device is removed from registry and MQTT connection is closed. No separate notification channel.
- Authentication is handled exclusively by yahabroker. The bridge forwards credentials as-is in the MQTT CONNECT. It performs no credential validation itself.
