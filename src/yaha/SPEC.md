# src/yaha — YAHA Home Automation Client Components

Top-level directory for all YAHA home automation client-side components. No source files live
directly here — each sub-topic has its own subdirectory.

## Sub-modules

| Directory    | Step | Description |
|--------------|------|-------------|
| `message/`   | 1    | Universal YAHA Message value type shared by all YAHA components. |
| `ini/`       | shared | Generic INI parser with section/key multi-value support reusable across YAHA clients. |
| `mqtt_component/` | 2 | Transport-agnostic IMqttComponent interface used by MQTT session/client wiring. |
| `mqtt_client/` | 3 | Reusable YahaMqttClient MQTT session loop with reconnect, subscribe replay, inbound dispatch, and keep-alive. |
| `broker_connector/` | broker connector phase 2 | Source HTTP MQTT adapter and lifecycle manager for connect-subscribe-reconnect on HTTP broker interface 1.0. |
| `message_store/` | 4-7 | Internal MessageTree plus persistence and MessageStore IMqttComponent logic for state/history queries, cleanup, disk restore/save, and HTTP query interface. |
| `message_store_client/` | 8 | Standalone MessageStore process composition, runtime config loader, and lifecycle orchestration for executable wiring. |
