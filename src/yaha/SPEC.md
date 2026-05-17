# src/yaha — YAHA Home Automation Client Components

Top-level directory for all YAHA home automation client-side components. No source files live
directly here — each sub-topic has its own subdirectory.

## Sub-modules

| Directory    | Step | Description |
|--------------|------|-------------|
| `message/`   | 1    | Universal YAHA Message value type shared by all YAHA components. |
| `error_handling/` | shared | Unified `YahaError` model for throw paths and API error outputs with code, technical message, user message, and optional debug details. |
| `ini/`       | shared | Generic INI parser with section/key multi-value support reusable across YAHA clients. |
| `mqtt_component/` | 2 | Transport-agnostic IMqttComponent interface used by MQTT session/client wiring. |
| `mqtt_client/` | 3 | Reusable YahaMqttClient MQTT session loop with reconnect, subscribe replay, inbound dispatch, and keep-alive. |
| `http_mqtt_interface/` | http mqtt interface phase 1 | Shared HTTP MQTT request/response contracts and low-level header/payload validators reused by all HTTP MQTT operations. |
| `http_mqtt_interface_client/` | http mqtt interface phase 8 | Standalone HTTP MQTT interface runtime process exposing publish/pubrel endpoints and browser compatibility routes using the shared interface module. |
| `broker_connector/` | broker connector phase 3 | Source HTTP adapter plus IMqttComponent relay component for source-to-target forwarding core. |
| `broker_connector_client/` | broker connector phase 4 | Standalone process config mapping for broker connector composition using generic mqtt runtime modules. |
| `file_store/` | filestore phase 1 | FileStore IMqttComponent with HTTP key/value API and MQTT monitoring publishes for file changes. |
| `file_store_client/` | filestore phase 2 | Standalone FileStore process config mapping and runtime composition wiring. |
| `remote_service/` | remoteservice phase 5 | RemoteService mapping lifecycle, domain command resolution/publish API, and lifecycle logging hooks for standalone runtime. |
| `remote_service_http/` | remoteservice phase 4 | HTTP GET/POST adapter with token validation hooks and domain publish response mapping. |
| `remote_service_client/` | remoteservice phase 6 | Standalone RemoteService INI mapping, executable composition/CLI runtime wiring, and deployment packaging integration (`remoteservice` component with binary, INI template, and service unit generation). |
| `zwave/` | zwave phase 6 | ZWave domain configuration plus IMqttComponent service orchestration for publish matcher flow, inbound command routing, lifecycle startup markers, and phase-6 service/component lifecycle verification tests. |
| `zwave_devices/` | zwave phase 2 | ZWave mapping and conversion helpers for value-to-topic resolution, topic-to-id lookup, and write payload normalization. |
| `zwave_controller/` | zwave phase 3 | ZWave controller adapter for callback event mapping, node/value cache management, and topic-based set routing via driver port abstraction. |
| `zwave_client/` | zwave phase 6 | Standalone ZWave runtime mapping and executable composition via `yaha_zwaveclient_main.cpp` with real OpenZWave runtime driver binding, watcher-to-controller event translation, generic MQTT runtime orchestration, and phase-6 runtime config validation tests. |
| `value_service/` | valueservice phase 2 | ValueService IMqttComponent with startup FileStore load, `/set` handling, retained replay publish, and monitor-triggered reload. |
| `value_service_client/` | valueservice phase 3 | Standalone ValueService runtime config mapping and process composition via `yaha_valueserviceclient_main.cpp`. |
| `message_store/` | 4-7 | Internal MessageTree plus persistence and MessageStore IMqttComponent logic for state/history queries, cleanup, disk restore/save, and HTTP query interface with ISO-8601 UTC `time` output fields. |
| `message_store_client/` | 8 | Standalone MessageStore process composition, runtime config loader, and lifecycle orchestration for executable wiring. |
| `automation/` | automation step 1 | Expression DSL tokenizer for YAHA automation rules engine. |
| `automation_client/` | automation step 2 | Standalone Automation IMqttComponent runtime config mapping, FileStore startup load, and MQTT-driven rule update synchronization. |
| `rs485_protocol/` | rs485 phase 2 | RS485 serial frame protocol codec for v0/v1, CRC/parity validation, and stream-reader parsing with legacy noise/error behavior. |
| `rs485_interface/` | rs485 phase 4 | MQTT <-> serial mapping helpers plus RS485 `IMqttComponent` runtime boundary for action handling, scheduler loops, and serial decode/publish routing. |
| `rs485_state/` | rs485 phase 3 | RS485 token state machine, token-exchange behavior, queue semantics, and tick scheduler implementation with legacy-compatible transitions and retry/dequeue rules. |
| `rs485_interface_client/` | rs485 phase 4 | Standalone RS485 runtime config contract, INI mapping, runtime composition wiring, and POSIX serial adapter boundary for the RS485 client process. |
