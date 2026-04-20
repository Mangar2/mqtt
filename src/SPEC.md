# src — Source Overview

Top-level source directory for the MQTT 5.0 broker.
Details for each module live in the `SPEC.md` files within the respective subdirectory.

## Modules

| Directory      | Plan ref | Description |
|----------------|----------|-------------|
| `data_model/`  | 1        | Pure data structures — primitive types, reason codes, properties, packet structs, message, subscription, session models. Header-only, no external dependencies. |
| `codec/`       | 2        | Serialization / deserialization of all MQTT 5.0 wire packets. Depends on `data_model/`. |
| `topic/`       | 3        | Topic-name and topic-filter validation, subscription trie storage, and topic matching. Depends on `data_model/`. |
| `store/`       | 4        | In-memory runtime state: subscription store, retained message store, session store, and inflight store. Depends on `data_model/` and `topic/`. |
| `qos/`         | 5        | QoS Engine — Packet ID allocation, QoS 1 and QoS 2 state machines with retransmission. Depends on `data_model/` and `store/`. |
| `network/`     | 6        | Network Layer — TCP listener, connection wrapper, incoming stream buffer, and outgoing write queue. No MQTT knowledge. No external dependencies. |
| `executor/`    | threading step 02 | Executor primitives — connection job model, concurrent queue, per-connection scheduler, pool scaling policy, and elastic worker pool. Not yet integrated into broker/connection runtime. |
| `connection/`  | 7, 23, 24 | Connection Handler + Connection Manager — per-connection lifecycle helpers, outbound-queue bridging utilities, dedicated listener/thread lifecycle management, and lean client I/O orchestration delegating workflow to `Broker` facades and `ClientSession`. |
| `auth/`        | 8        | Authentication Module — pluggable authenticator interface, username/password and anonymous authenticators, and enhanced AUTH packet handler. |
| `authz/`            | 9        | Authorization Module — ACL engine with MQTT wildcard topic matching, ACL rule structure, configuration loader with runtime reload, and broker startup ACL policy helpers. |
| `session_manager/`  | 10       | Session Manager — session lifecycle controller (create/resume/discard), session takeover handler (Client ID collision, Reason 0x8E), and session expiry scheduler. Depends on `store/`, `connection/`, `auth/`. |
| `will_manager/`     | 11       | Will Manager — will store, will-delay timer, and will publisher. Stores Will Messages on connect, suppresses them on normal disconnect, and publishes them (with optional delay) on connection loss or session expiry. Depends on `data_model/`, `store/`, `session_manager/`. |
| `transport/`        | 14.2     | Transport Extensions — WebSocket HTTP upgrade handshake, WebSocket frame encoder/decoder, and MQTT payload extraction from binary frames. **Module 14.1 (TLS) is not implemented** — use a reverse proxy for TLS termination. |
| `broker/`           | 15, 17, 18, 19, threading step 03 | Broker Orchestrator + Facades — INI configuration loader, component wiring, ordered startup/shutdown, signal handling, and extracted connect/disconnect/publish/subscribe/tick facades with per-facade locking (no broker-wide mutex). |
| `monitoring/`       | 16, 26   | Monitoring + Structured Tracing — `StatisticsCollector` (connected clients, message throughput, active subscriptions, retained messages, uptime), `SysTopicPublisher` (periodic `$SYS/broker/…` topic publication), `StructuredTracer` (JSON-lines runtime tracing with global threshold and per-module trace overrides), and runtime trace command parsing for `$SYS` control messages. |
| `subscription_manager/` | 19 | Subscription orchestration module — validates SUBSCRIBE/UNSUBSCRIBE options, parses shared filters, coordinates ACL checks, updates subscription store/shared dispatcher, and builds SUBACK/UNSUBACK reason results. |
| `outbound_queue/`   | 20       | Outbound Message Queue — thread-safe per-client FIFO of `Message` objects. Decouples publishing thread from receiving client's QoS state. Depends on `data_model/`. |
| `client_session/`   | 21       | Client Session Context — per-client packet handlers and local state bundle (`PacketIdManager`, QoS 1/2 state machines, keep-alive, receive-maximum, aliases, enhanced auth, outbound drain). Depends on `codec/`, `qos/`, `connection/`, `outbound_queue/`. |

## `data_model/` sub-modules

| Directory              | Plan ref | Contents |
|------------------------|----------|----------|
| `data_model/types/`       | 1.1      | Primitive MQTT wire types (VarByteInt, UTF-8 string, binary data, integers) |
| `data_model/reason_code/` | 1.2      | Reason code enum (39 values) and success/error classification |
| `data_model/property/`    | 1.3      | Property identifier enum and value/mapping types |
| `data_model/packet/`      | 1.4      | Struct definitions for all 15 MQTT packet types |
| `data_model/message/`     | 1.5      | Message and WillMessage model |
| `data_model/subscription/`| 1.6      | Subscription, SubscriptionOptions, SharedSubscription |
| `data_model/session/`     | 1.7      | SessionState, InflightEntry and supporting enums |

## `codec/` sub-modules

| Directory / File           | Plan ref  | Contents |
|----------------------------|-----------|----------|
| `codec/primitive/`         | 2.1       | Encode / decode of all MQTT primitive wire types |
| `codec/properties/`        | 2.2       | Properties section encoder, decoder, validator |
| `codec/fixed_header/`      | 2.3       | Fixed-header encoder and decoder |
| `codec/packet/`            | 2.4–2.12  | Variable-header + payload codecs for all 15 control packets |
| `codec/codec_error.h`      | —         | `CodecError` enum and `CodecException` |
| `codec/read_buffer.h`      | —         | `ReadBuffer` — read-only cursor over a byte span |
| `codec/write_buffer.h`     | —         | `WriteBuffer` — alias for `std::vector<uint8_t>` |

## `topic/` sub-modules

| Directory / File                     | Plan ref | Contents |
|--------------------------------------|----------|----------|
| `topic/topic_error.h`                | 3.1      | `TopicError` enum and `TopicException` |
| `topic/topic_validator.h/.cpp`       | 3.1      | `validate_topic_name`, `validate_topic_filter`, `is_system_topic` |
| `topic/subscription_trie.h/.cpp`     | 3.2      | `SubscriptionTrie` — trie storage for MQTT subscriptions (insert, remove, remove_all) |
| `topic/topic_matcher.h/.cpp`         | 3.3      | `TopicMatcher` — matches a publish topic name against all trie subscriptions (exact, `+`, `#`, system-topic exclusion) |

## `store/` sub-modules

| Directory / File                              | Plan ref | Contents |
|-----------------------------------------------|----------|----------|
| `store/store_error.h`                          | 4        | `StoreError` enum and `StoreException` |
| `store/subscription_store.h/.cpp`              | 4.1      | In-memory subscription store; wraps `SubscriptionTrie` + `TopicMatcher` |
| `store/retained_message_store.h/.cpp`          | 4.2      | In-memory map of retained messages keyed by topic name |
| `store/session_store.h/.cpp`                   | 4.3      | In-memory map of `SessionState` records keyed by client ID |
| `store/inflight_store.h/.cpp`                  | 4.4      | Per-session `InflightEntry` arrays; tracks in-use packet IDs |

## `qos/` sub-modules

| Directory / File                              | Plan ref | Contents |
|-----------------------------------------------|----------|----------|
| `qos/qos_error.h`                              | 5        | `QosError` enum and `QosException` |
| `qos/packet_id_manager.h/.cpp`                 | 5.1      | Per-session Packet Identifier allocator; separate inbound/outbound spaces |
| `qos/qos1_state_machine.h/.cpp`                | 5.2      | QoS 1 (AtLeastOnce) inbound/outbound handshake and retransmission logic |
| `qos/qos2_state_machine.h/.cpp`                | 5.3      | QoS 2 (ExactlyOnce) inbound/outbound handshake, duplicate detection, retransmission |

## `network/` sub-modules

| Directory / File                              | Plan ref | Contents |
|-----------------------------------------------|----------|----------|
| `network/network_error.h`                      | 6        | `NetworkError` enum and `NetworkException` |
| `network/tcp_connection.h/.cpp`                | 6.1.3    | `TcpConnection` — owns a connected socket fd; blocking read/write/close |
| `network/tcp_listener.h/.cpp`                  | 6.1.1–2  | `TcpListener` — opens server socket, blocks on accept, hands off connections |
| `network/stream_buffer.h/.cpp`                 | 6.2      | `StreamBuffer` — accumulates TCP bytes, extracts complete MQTT packets via VBI framing |
| `network/write_queue.h/.cpp`                   | 6.3      | `WriteQueue` — thread-safe outgoing packet queue; async drain with backpressure |
| `network/socket_ops.h/.cpp`                    | step 01  | Non-blocking socket helper functions (`set_nonblocking`, `nb_read`, `nb_write`, `nb_accept`) |
| `network/connection_slot.h/.cpp`               | step 01  | Per-connection fd + read/write ring-buffer state and connection phase tracking |
| `network/connection_table.h/.cpp`              | step 01  | `ConnectionTable` — thread-safe ownership table of `ConnectionSlot` instances by fd |

## `connection/` sub-modules

| Directory / File | Plan ref | Contents |
|------------------|----------|----------|
| `connection/connection_error.h` | 7 | `ConnectionError` enum and `ConnectionException` |
| `connection/connection_state.h/.cpp` | 7.1 | `ConnectionStateMachine` — lifecycle states Connecting → Connected → Disconnecting → Closed |
| `connection/keep_alive_timer.h/.cpp` | 7.2 | `KeepAliveTimer` — 1.5 × Keep Alive deadline with reset-on-packet |
| `connection/topic_alias_table.h/.cpp` | 7.3 | `TopicAliasTable` — inbound and outbound alias↔topic mappings with maximum enforcement |
| `connection/receive_maximum.h/.cpp` | 7.4 | `ReceiveMaximum` — inflight QoS 1/2 packet counter with pause/resume flow control |
| `connection/outbound_queue_bridge.h/.cpp` | 24 | Outbound queue bridge helpers for draining and transferring pending messages |
| `connection/connection_manager.h/.cpp` | 23 | `ConnectionManager` — owns listeners, accept-loop threads, and tracked client-thread lifecycle |

## `executor/` sub-modules

| Directory / File | Plan ref | Contents |
|------------------|----------|----------|
| `executor/connection_job.h` | threading step 02 | `ConnectionJob` value type with `JobType` and payload variant |
| `executor/job_queue.h/.cpp` | threading step 02 | `JobQueue` — concurrent blocking FIFO for `ConnectionJob` |
| `executor/pool_scaling_policy.h/.cpp` | threading step 02 | Pure scale-up decision helper `should_grow(...)` |
| `executor/job_scheduler.h/.cpp` | threading step 02 | `JobScheduler` — per-fd serialization and deferred backlog |
| `executor/worker_pool.h/.cpp` | threading step 02 | `WorkerPool` — elastic worker-thread execution on queued jobs |

## `auth/` sub-modules

| Directory / File | Plan ref | Contents |
|------------------|----------|----------|
| `auth/auth_error.h`              | 8   | `AuthError` enum and `AuthException` |
| `auth/authenticator.h/.cpp`      | 8.1 | `AuthStatus`, `AuthResult`, `IAuthenticator` abstract base, `CallbackAuthenticator` |
| `auth/anonymous_authenticator.h/.cpp` | 8.4 | `AnonymousAuthenticator` — policy-driven allow/deny without inspecting credentials |
| `auth/password_authenticator.h/.cpp`  | 8.2 | `PasswordAuthenticator` — username/password credential store and validator |
| `auth/enhanced_auth_handler.h/.cpp`   | 8.3 | `EnhancedAuthHandler` — AUTH packet state machine for multi-step and re-authentication |

## `authz/` sub-modules

| Directory / File | Plan ref | Contents |
|------------------|----------|----------|
| `authz/authz_error.h`      | 9     | `AuthzError` enum and `AuthzException` |
| `authz/acl_rule.h`         | 9.1.1 | `AclAction`, `AclEffect`, `AclRule` aggregate |
| `authz/acl_engine.h/.cpp`  | 9.1   | `AclEngine` — ordered rule evaluation with MQTT wildcard topic matching |
| `authz/acl_loader.h/.cpp`  | 9.2   | `AclLoader` — parse `AclRuleConfig` strings into rules; initial load and runtime reload |
| `authz/broker_acl_policy.h/.cpp` | 9 | Startup ACL policy helpers for broker-internal and anonymous fallback rules |

## `session_manager/` sub-modules

| Directory / File | Plan ref | Contents |
|------------------|----------|----------|
| `session_manager/session_manager_error.h`       | 10   | `SessionManagerError` enum and `SessionManagerException` |
| `session_manager/session_open_result.h`          | 10.1 | `SessionOpenResult` — result of `handle_connect` carrying `session_present` and `takeover_occurred` |
| `session_manager/session_manager.h/.cpp`         | 10.1 | `SessionManager` — lifecycle coordinator: create/resume session, disconnect, cleanup expired |
| `session_manager/session_takeover_handler.h/.cpp`| 10.2 | `SessionTakeoverHandler` — tracks active connections; closes old connection with 0x8E on Client ID collision |
| `session_manager/session_expiry_scheduler.h/.cpp`| 10.3 | `SessionExpiryScheduler` — records disconnect timestamps; reports sessions past their expiry deadline |

## `will_manager/` sub-modules

| Directory / File | Plan ref | Contents |
|------------------|----------|----------|
| `will_manager/will_manager_error.h`    | 11   | `WillManagerError` enum and `WillManagerException` |
| `will_manager/will_store.h/.cpp`       | 11.1 | `WillStore` — in-memory map of `WillMessage` records keyed by Client ID |
| `will_manager/will_delay_timer.h/.cpp` | 11.2 | `WillDelayTimer` — per-client disconnect timestamp + will-delay interval tracking |
| `will_manager/will_publisher.h/.cpp`   | 11.3 | `WillPublisher` — orchestrates store, timer, and publish decisions |

## `broker/` (Modules 15, 17, 18, 19)

| File | Plan ref | Contents |
|------|----------|----------|
| `broker/broker_error.h`     | 15   | `BrokerError` enum and `BrokerException` |
| `broker/broker_config.h`    | 15.1 | `BrokerConfig` struct — all configuration parameters |
| `broker/config_loader.h/.cpp` | 15.1.1 | `ConfigLoader` — INI-file parser → `BrokerConfig` |
| `broker/broker.h/.cpp`      | 15.2–15.3, 17, 18, 19, threading step 03 | `Broker` — thin orchestrator/delegator for startup, shutdown, module wiring, and facade dispatch |
| `broker/connect_result.h` | 18.1 | `ConnectResult` handshake outcome value type used by broker/connect facade |
| `broker/enhanced_auth_registry.h/.cpp` | threading step 03 | `EnhancedAuthRegistry` — thread-safe pending/active enhanced AUTH state |
| `broker/connect_facade.h/.cpp` | threading step 03 | `ConnectFacade` — CONNECT, AUTH continuation, and re-authentication workflow |
| `broker/disconnect_facade.h/.cpp` | threading step 03 | `DisconnectFacade` — disconnect/connection-lost handling and expiry-override validation |
| `broker/publish_facade.h/.cpp` | threading step 03 | `PublishFacade` — inbound publish routing + reason mapping + tracing |
| `broker/subscribe_facade.h/.cpp` | threading step 03 | `SubscribeFacade` — subscribe/unsubscribe orchestration + tracing |
| `broker/tick_handler.h/.cpp` | threading step 03 | `TickHandler` — periodic housekeeping tick and runtime trace command application |
| `broker/broker_module_factory.h/.cpp` | threading step 03 | `BrokerModuleFactory` — module creation/wiring extracted from broker |
| `broker/persistence_coordinator.h/.cpp` | threading step 03 | `PersistenceCoordinator` — load/flush persistence snapshots |

## `subscription_manager/` (Module 19)

| File | Plan ref | Contents |
|------|----------|----------|
| `subscription_manager/subscription_orchestrator.h/.cpp` | 19 | `SubscriptionOrchestrator` — SUBSCRIBE/UNSUBSCRIBE orchestration, shared filter parsing, ACL checks, store/dispatcher coordination, retained delivery |

## Entry point

| File       | Contents |
|------------|----------|
| `main.cpp` | Application entry point (broker startup) |
