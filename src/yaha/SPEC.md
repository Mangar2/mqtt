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
| `broker_connector/` | broker connector phase 3 | Source HTTP adapter plus IMqttComponent relay component for source-to-target forwarding core. |
| `broker_connector_client/` | broker connector phase 4 | Standalone process config mapping for broker connector composition using generic mqtt runtime modules. |
| `file_store/` | filestore phase 1 | FileStore IMqttComponent with HTTP key/value API and MQTT monitoring publishes for file changes. |
| `file_store_client/` | filestore phase 2 | Standalone FileStore process config mapping and runtime composition wiring. |
| `message_store/` | 4-7 | Internal MessageTree plus persistence and MessageStore IMqttComponent logic for state/history queries, cleanup, disk restore/save, and HTTP query interface. |
| `message_store_client/` | 8 | Standalone MessageStore process composition, runtime config loader, and lifecycle orchestration for executable wiring. |
| `automation/` | automation step 1 | Expression DSL tokenizer for YAHA automation rules engine. |
| `automation_client/` | automation step 2 | Standalone Automation IMqttComponent runtime config mapping, FileStore startup load, and MQTT-driven rule update synchronization. |
