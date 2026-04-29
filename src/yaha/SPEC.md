# src/yaha — YAHA Home Automation Client Components

Top-level directory for all YAHA home automation client-side components. No source files live
directly here — each sub-topic has its own subdirectory.

## Sub-modules

| Directory    | Step | Description |
|--------------|------|-------------|
| `message/`   | 1    | Universal YAHA Message value type shared by all YAHA components. |
| `mqtt_component/` | 2 | Transport-agnostic IMqttComponent interface used by MQTT session/client wiring. |
| `mqtt_client/` | 3 | Reusable YahaMqttClient MQTT session loop with reconnect, subscribe replay, inbound dispatch, and keep-alive. |
| `message_store/` | 4-5 | Internal MessageTree plus persistence for MessageStore state, history, subtree queries, snapshot diff, cleanup, and disk restore/save. |
