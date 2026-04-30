# HTTP-Modified MQTT Interface 1.0

## Purpose

Defines the HTTP request and response contract used by mqtt-utils for MQTT operations in interface version 1.0.
This document describes only version 1.0 behavior and data shapes.

## Scope

Covered operation pairs:

- connect / onConnect
- disconnect / onDisconnect
- publish / onPublish
- pubrel / onPubrel
- subscribe / onSubscribe
- unsubscribe / onUnsubscribe

Covered shared contract elements:

- shared headers and result types from interfaces.ts
- dispatcher behavior in index.ts for selecting the version implementation

## Shared Types And Headers

### topics_t

Mapping of topic string to QoS number:

- shape: { [topic: string]: qos_t }

### headers_t

- type: IncomingHttpHeaders

### IResult

Response envelope used by all resultCheck handlers and onX functions.

Required fields:

- statusCode: number
- headers: headers_t
- payload: string

Optional fields:

- present: 0 | 1
- token: string
- runtime: number
- packetid: number

### RequestDataV2

Request envelope returned by all v1.0 request builders:

- headers: Record<string, string>
- payload: Record<string, any>
- resultCheck: (result: IResult) => void

### Standard Header Constants

application/json default headers:

- content-type: application/json; charset=UTF-8
- accept: application/json,text/plain
- accept-charset: UTF-8

text/plain default headers:

- content-type: text/plain; charset=UTF-8
- accept: application/json,text/plain
- accept-charset: UTF-8

## Version Dispatch Rules

The dispatcher uses headers.version to choose onX handlers.

- If headers.version is not a string, default version is 0.0.
- If the selected version key is not implemented in onPublish map, dispatch throws: undefined version <version>.

This behavior is implementation-defined in index.ts and applies to all onX dispatchers through getVersion.

## Connect 1.0

### Request Builder

Input type ConnectOptions:

- qos?: qos_t
- clientId?: string
- version?: string
- host?: string
- port?: number
- clean: boolean
- keepAlive?: number
- password?: string
- user?: string
- will?: IMessage

Request output:

- headers: standard JSON headers plus version=1.0
- payload: shallow copy of options
- payload.keepAlive is forced to 0 when undefined

### resultCheck Rules

Expected response:

- statusCode must be 200
- headers.content-type must start with application/json
- headers.packet must be connack
- payload must be stringified JSON object

Parsed payload must satisfy:

- present is exactly 0 or 1
- mqttcode is undefined or 0
- if mqttcode is 1..5, throw mapped MQTT error text
- token.send and token.receive must both be strings

### onConnect Response

Return object:

- statusCode: 200
- headers.content-type: application/json; charset=UTF-8
- headers.packet: connack
- headers.version: 1.0
- payload: JSON.stringify(ConnectResult)

ConnectResult shape:

- mqttcode?: 0 | 1 | 2 | 3 | 4 | 5
- present: number
- token: { send: string, receive: string }

## Disconnect 1.0

### Request Builder

Input:

- clientId: string

Request output:

- headers: standard JSON headers plus version=1.0
- payload: { clientId }

### resultCheck Rules

- statusCode must be 204

### onDisconnect Response

Return object:

- statusCode: 204
- headers.content-type: application/json; charset=UTF-8
- headers.version: 1.0
- payload: empty string

## Publish 1.0

### Request Builder

Input type IPublishOptions:

- token: string
- message: IMessage
- dup?: boolean
- packetid?: number

Derived values:

- qos defaults to 1 when message.qos is undefined
- retain defaults to false when message.retain is undefined
- dup defaults to 0/false when undefined

Request output:

- headers: standard JSON headers
- headers.qos: qos as string
- headers.dup: 1 or 0
- headers.retain: 1 or 0
- headers.version: 1.0
- headers.packetid set only if packetid is number or string
- payload shape:
  - token: string
  - message:
    - topic
    - value
    - reason

Only topic, value, reason are forwarded from message.

### resultCheck Rules

Expected response:

- statusCode must be 204
- headers.packetid must equal request packetid string when packetid was provided
- for qos=1: headers.packet must be puback
- for qos=2: headers.packet must be pubrec
- qos=0 does not enforce packet header value

### onPublish Response

Input headers are inspected:

- qos = Number(headers.qos)
- packetid = headers.packetid

Return object:

- statusCode: 204
- headers.content-type: application/json; charset=UTF-8
- headers.version: 1.0
- headers.qos: original headers.qos
- headers.retain: original headers.retain
- payload: empty string
- packetid field set as number when packetid header exists
- headers.packetid echoed when packetid header exists
- headers.packet:
  - puback if qos==1
  - pubrec if qos==2

## Pubrel 1.0

### Request Builder

Input:

- token: string
- packetid?: string

Request output:

- headers: standard text headers plus version=1.0
- headers.packetid set only if packetid is number or string
- payload: { token }

### resultCheck Rules

Expected response:

- statusCode must be 204
- headers.packetid must match request packetid when provided
- headers.packet must be pubcomp

### onPubrel Response

Input header:

- packetid from headers.packetid

Return object:

- statusCode: 204
- headers.content-type: application/json; charset=UTF-8
- headers.version: 1.0
- headers.packet: pubcomp
- headers.packetid: echoed
- payload: empty string
- packetid field: Number(packetid)

## Subscribe 1.0

### Request Builder

Input:

- topics: topics_t
- clientId: string
- packetid: number

Request output:

- headers: standard JSON headers plus version=1.0 and packetid as string
- payload: { clientId, topics }

### resultCheck Rules

Expected response:

- statusCode must be 200
- headers.content-type must start with application/json
- headers.packet must be suback
- headers.packetid numeric value must equal request packetid
- payload must be JSON string object with qos array

Accepted qos return codes:

- 0
- 1
- 2
- 0x7F
- 0x80

### onSubscribe Response

Input:

- headers.packetid
- result: { qos: SubscribeReturnCodes[] }

Return object:

- statusCode: 200
- headers.content-type: application/json; charset=UTF-8
- headers.version: 1.0
- headers.packet: suback
- headers.packetid: echoed
- payload: JSON.stringify(result)
- packetid field: Number(headers.packetid)

## Unsubscribe 1.0

### Request Builder

Input:

- topics: topics_t
- clientId: string
- packetid: number

Request output:

- headers: standard JSON headers plus version=1.0 and packetid as string
- payload: { topics, clientId }

### resultCheck Rules

Expected response:

- statusCode must be 200 or 204
- headers.content-type must start with application/json
- headers.packet must be unsuback
- headers.packetid numeric value must equal request packetid
- payload must be string

Backward compatibility rule:

- if statusCode==204 and payload is empty string, validation succeeds without JSON parsing

Otherwise payload must parse as array of return codes.

Accepted unsubscribe return codes:

- 0x00
- 0x11

### onUnsubscribe Response

Input:

- headers.packetid
- result: UnsubscribeResult (array of 0x00 | 0x11)

Return object:

- statusCode: 200
- headers.content-type: application/json; charset=UTF-8
- headers.packet: unsuback
- headers.version: 1.0
- headers.packetid: echoed
- payload: JSON.stringify(result)
- packetid field: Number(packetid)

## Exported API Surface

Top-level exported entry is Interfaces object with methods:

- publish(version, options)
- onPublish(headers)
- pubrel(version, token, packetid)
- onPubrel(headers)
- subscribe(version, topics, clientId, packetid)
- onSubscribe(headers, result)
- unsubscribe(version, topics, clientId, packetid)
- onUnsubscribe(headers, result)
- connect(version, options)
- onConnect(headers, payload)
- disconnect(version, clientId)
- onDisconnect(headers)

For this specification, only version=1.0 behavior is normative.
