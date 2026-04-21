# broker — Broker Orchestrator + Extracted Facades (Modules 15, 17, 18, 19 + Threading Step 03)

Wires all modules together, controls broker startup / shutdown, and provides
thread-safe access to shared broker state.
Depends on: all previous modules (1–14), plus shared-state coordination for Module 17.

---

## Responsibilities

- Load and validate broker configuration from a text file (INI format).
- Instantiate every module object and inject its dependencies.
- Bind persistence adapters to in-memory stores on startup.
- Delegate listener and connection-thread lifecycle to `ConnectionManager`.
- Install SIGTERM / SIGINT signal handlers.
- Perform ordered startup and ordered shutdown.
- Track active connections so the message router can deliver to online clients.
- Delegate connect/disconnect/publish/subscribe/tick workflows to dedicated facade classes.
- Keep thread safety through per-component locks (registry + each facade), not a broker-wide mutex.
- Provide a high-level connect facade that returns full handshake outcome data.
- Keep broker as thin startup/shutdown + delegation layer.

---

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `broker_error.h`    | 15   | `BrokerError` enum and `BrokerException` |
| `broker_config.h`   | 15.1 | `BrokerConfig` struct with all configuration parameters |
| `config_loader.h/.cpp` | 15.1.1 | `ConfigLoader` — INI-file parser → `BrokerConfig` |
| `connack_properties.h/.cpp` | 18.2 | Build static and CONNECT-driven CONNACK properties (server capabilities and optional response information) |
| `connect_result.h` | 18.1 | `ConnectResult` value type for connect/auth handshake outcomes |
| `active_connection_registry.h/.cpp` | 20.2 | `ActiveConnectionRegistry` — thread-safe online connection map with dedicated lock |
| `enhanced_auth_registry.h/.cpp` | threading step 03 | `EnhancedAuthRegistry` — thread-safe pending and active enhanced-auth state |
| `connect_facade.h/.cpp` | threading step 03 | `ConnectFacade` — CONNECT + AUTH + re-authentication logic |
| `disconnect_facade.h/.cpp` | threading step 03 | `DisconnectFacade` — disconnect and connection-loss handling |
| `publish_facade.h/.cpp` | threading step 03 | `PublishFacade` — inbound publish handling and reason-code mapping |
| `subscribe_facade.h/.cpp` | threading step 03 | `SubscribeFacade` — subscribe/unsubscribe orchestration + tracing |
| `tick_handler.h/.cpp` | threading step 03 | `TickHandler` — housekeeping tick + trace runtime command handling |
| `broker_module_factory.h/.cpp` | threading step 03 | `BrokerModuleFactory` — module construction/wiring extracted from broker |
| `persistence_coordinator.h/.cpp` | threading step 03 | `PersistenceCoordinator` — persistence load/flush orchestration |
| `broker.h/.cpp`     | 15.2–15.3, 17, 18, 19, threading step 03 | `Broker` — thin orchestrator/delegator for startup/shutdown, signal handling, and facade dispatch |

---

## Configuration File Format (INI)

Comments begin with `#`.  Sections are delimited by `[name]`.
Key-value pairs use `key = value` (whitespace around `=` is ignored).
Unknown keys and sections are silently ignored.
All parameters are optional; absent ones keep their default value.

### Sections and keys

| Section       | Key                   | Type     | Default | Description |
|---------------|-----------------------|----------|---------|-------------|
| `[network]`   | `mqtt_port`           | uint16   | `1883`  | MQTT/TCP listener port. `0` = disabled. |
| `[network]`   | `ws_port`             | uint16   | `0`     | WebSocket listener port. `0` = disabled. |
| `[broker]`    | `allow_anonymous`     | bool     | `true`  | Accept connections without credentials. |
| `[broker]`    | `max_connections`     | uint32   | `1000`  | Maximum simultaneous client connections. |
| `[broker]`    | `receive_maximum`     | uint16   | `65535` | Per-connection inflight QoS 1/2 limit. |
| `[broker]`    | `server_keep_alive`   | uint16   | `0`     | CONNACK Server Keep Alive override. `0` = disabled (use CONNECT keep alive). |
| `[broker]`    | `session_expiry_max`  | uint32   | `0`     | Hard cap on session expiry seconds. `0` = unlimited. |
| `[broker]`    | `topic_alias_maximum` | uint16   | `10`    | Maximum topic alias value per connection. |
| `[broker]`    | `max_queued_messages` | uint32   | `100`   | Per-client offline queue capacity. |
| `[broker]`    | `write_queue_max_bytes` | uint32 | `65536` | Per-connection `WriteQueue` byte capacity. |
| `[broker]`    | `qos_retransmit_timeout_seconds` | uint32 | `20` | Timeout before outbound QoS 1/2 retransmission is eligible in `ClientSession`. |
| `[broker]`    | `tick_interval_ms`    | uint32   | `100`   | Main loop sleep interval between broker housekeeping ticks. |
| `[auth]`      | `credential`          | string   | —       | Repeated `username:password` entries for `PasswordAuthenticator`. |
| `[acl]`       | `rule`                | csv      | —       | Repeated `effect,principal,action,topic` ACL entries. |
| `[persistence]` | `mode`              | enum     | `full`  | Persistence mode: `full`, `off`, `no-states`. |
| `[persistence]` | `enabled`           | bool     | —       | Legacy compatibility key mapped to `mode` (`true`→`full`, `false`→`off`). |
| `[persistence]` | `dir`               | string   | `./data`| Directory for persistence snapshot files. |
| `[tracing]`   | `global_level`      | enum     | `warning` | Global structured trace level (`none|error|warning|info|trace`). |
| `[tracing]`   | `trace_modules`     | csv      | `""` | Comma-separated modules with trace override enabled. |

### Validation rules

- `mqtt_port` and `ws_port`: `0` disables the listener; `1–65535` are valid non-zero values.  
  At least one of `mqtt_port` or `ws_port` must be non-zero (enforced by `ConfigLoader`).
- `max_connections`: `1–100 000`.
- `receive_maximum`: `1–65535`.
- `server_keep_alive`: `0–65535`. `0` disables the override.
- `topic_alias_maximum`: `0–65535`. `0` disables topic aliases.
- `max_queued_messages`: `1–100 000`.
- `write_queue_max_bytes`: `1–4 194 304` (hard upper bound to avoid unbounded
   per-connection buffering).
- `session_expiry_max`: `0–4 294 967 295` (any uint32 value; `0` = no hard cap).

---

## Public API

### `BrokerError` / `BrokerException`

```cpp
enum class BrokerError : uint8_t {
    InvalidConfig,       ///< A configuration value is out of range or malformed.
    NoListenerConfigured,///< Neither mqtt_port nor ws_port is non-zero.
    AlreadyRunning,      ///< startup() called while already running.
    NotRunning,          ///< shutdown() called when not running.
};
```

---

### `BrokerConfig` (15.1.2 + 15.1.3)

```cpp
struct BrokerConfig {
    uint16_t mqtt_port             = 1883;
    uint16_t ws_port               = 0;
    bool     allow_anonymous       = true;
    uint32_t max_connections       = 1000;
    uint16_t receive_maximum       = 65535;
   uint16_t server_keep_alive     = 0;
    uint32_t session_expiry_max    = 0;
    uint16_t topic_alias_maximum   = 10;
    uint32_t max_queued_messages   = 100;
   uint32_t write_queue_max_bytes = 65536;
   uint32_t qos_retransmit_timeout_seconds = 20;
   uint32_t tick_interval_ms      = 100;
   std::vector<PasswordCredentialConfig> password_credentials;
   std::vector<AclRuleConfig> acl_rules;
    PersistenceMode persistence_mode = PersistenceMode::Full;
    std::filesystem::path persistence_dir = "./data";
   TraceLevel trace_global_level  = TraceLevel::Warning;
   std::vector<std::string> trace_modules;
};
```

---

### `ConfigLoader` (15.1.1)

```cpp
// Load from file path (15.1.1)
[[nodiscard]] static BrokerConfig load(const std::filesystem::path& path);

// Parse from an in-memory string (useful for testing)
[[nodiscard]] static BrokerConfig parse(std::string_view text);
```

`load()` opens the file, reads its contents and delegates to `parse()`.
`parse()` tokenises the text, applies all key-value pairs to a `BrokerConfig`,
then validates the result; throws `BrokerException(InvalidConfig)` on any
invalid value and `BrokerException(NoListenerConfigured)` when both ports are 0.

---

### `Broker` (15.2 + 15.3)

```cpp
explicit Broker(BrokerConfig config);
~Broker();                         // calls shutdown() if still running

// 15.3.1 ordered startup
void startup();

// 15.3.2 ordered shutdown
void shutdown() noexcept;

[[nodiscard]] bool is_running() const noexcept;

// Module accessors — valid after startup()
[[nodiscard]] SessionManager&   session_manager()  noexcept;
[[nodiscard]] MessageRouter&    message_router()   noexcept;
[[nodiscard]] IAuthenticator&   authenticator()    noexcept;
[[nodiscard]] AclEngine&        acl_engine()       noexcept;
[[nodiscard]] WillPublisher&    will_publisher()   noexcept;
[[nodiscard]] StructuredTracer& structured_tracer() noexcept;

struct ConnectResult {
   AuthStatus auth_status;
   bool session_present;
   ReasonCode reason_code;
   std::optional<BinaryData> auth_data;
   std::string auth_method;
   std::vector<Property> connack_properties;
   std::string client_id;
};

// Thread-safe connect/disconnect facades (Modules 17.3 + 18)
ConnectResult handle_connect(const ConnectPacket& connect_packet,
                             std::function<void()> close_callback);
ConnectResult handle_auth_packet(std::string_view client_id,
                                 const AuthPacket& auth_packet);
AuthResult handle_reauthenticate(std::string_view client_id,
                                 const AuthPacket& auth_packet);
void handle_disconnect(std::string_view client_id, ReasonCode reason_code,
                       std::optional<uint32_t> expiry_override,
                       std::chrono::steady_clock::time_point now);

void handle_connection_lost(std::string_view client_id,
                            std::chrono::steady_clock::time_point now);

// Thread-safe subscribe/unsubscribe/publish facades (Module 19)
[[nodiscard]] SubackPacket handle_subscribe(std::string_view client_id,
                                            const SubscribePacket& packet);
[[nodiscard]] UnsubackPacket handle_unsubscribe(std::string_view client_id,
                                                const UnsubscribePacket& packet);
ReasonCode handle_publish(Message& msg, std::string_view client_id,
                          std::string_view username, TopicAliasTable& alias_table);

bool tick(std::chrono::steady_clock::time_point now =
          std::chrono::steady_clock::now());

void apply_trace_system_message(const Message& message);

// Connection registration — call from connection handler (Module 20.2)
void register_connection(std::string_view client_id,
                         std::shared_ptr<OutboundQueue> queue);
void unregister_connection(std::string_view client_id) noexcept;

// 15.3.3 signal handling
static void install_signal_handlers() noexcept;
[[nodiscard]] static bool shutdown_requested() noexcept;
```

#### Startup sequence (15.3.1)

1. Throw `BrokerException(AlreadyRunning)` if already running.
2. Instantiate all module objects in dependency order.
3. Configure authenticator according to `allow_anonymous`.
4. Load ACL via `authz/broker_acl_policy` helper: internal principal allow
   rule, configured `[acl] rule` entries, and optional anonymous fallback.
5. If `persistence_mode != Off`: call `load_persistence()` to populate in-memory
   stores from the snapshot files.
6. Start `ConnectionManager` (opens configured listener sockets and starts accept loops).
7. Set `running_ = true`.

`load_persistence()` restores three persistence snapshots:
- sessions into `SessionStore`
- retained messages into `RetainedMessageStore`
- inflight entries into `InflightStore` (only when `persistence_mode == Full`)

For persisted sessions, all non-shared subscriptions stored in each
`SessionState.subscriptions` entry are also re-registered in
`SubscriptionStore` so topic routing after restart is consistent with the
persisted session state.

#### Shutdown sequence (15.3.2)

1. Return immediately if not running.
2. Stop all active `OutboundQueue`s and stop `ConnectionManager`.
3. If `persistence_mode != Off`: call `flush_persistence()` to write current
   store contents to snapshot files.
4. Set `running_ = false`.

#### Will publish callback

Will messages published by `WillPublisher` are routed via
`MessageRouter::route_internal()` using the reserved internal principal.
An unconditional ACL allow-all rule is registered for this principal during
startup so that server-originated publishes pass authorisation.

#### Signal handling (15.3.3)

`install_signal_handlers()` registers a C signal handler for both `SIGTERM`
and `SIGINT` that sets the internal `shutdown_requested_` atomic flag to `true`.
Callers should poll `Broker::shutdown_requested()` in their main loop and call
`shutdown()` when it returns `true`.

#### Concurrency layer (17 + threading step 03)

- `Broker` does not own a global broker mutex anymore.
- Thread safety is partitioned:
  - `ActiveConnectionRegistry` owns map synchronization.
  - `EnhancedAuthRegistry` owns enhanced-auth state synchronization.
  - `ConnectFacade`, `DisconnectFacade`, `PublishFacade`, `SubscribeFacade`,
    and `TickHandler` each own a dedicated mutex for their workflow-critical
    sequences.
- Public `Broker` methods delegate to the respective facade.

#### Connect facade (18)

- `handle_connect()` authenticates CONNECT first and returns auth failure as
   `ConnectResult.reason_code`.
- For enhanced-auth CONNECT packets (`AuthenticationMethod` present),
   `handle_connect()` may return `auth_status == Continue` and
   `reason_code == ContinueAuthentication`.
- `handle_auth_packet()` advances pending enhanced-auth exchanges after
   `handle_connect()` returned Continue.
- `handle_reauthenticate()` handles AUTH(0x19 ReAuthenticate) for clients that
   previously completed enhanced authentication.
- On authentication success, it delegates session open/resume to
   `SessionManager::handle_connect()`.
- If CONNECT contains a Will, `Broker` extracts `WillMessage` internally and
   stores it via `WillPublisher::on_connect()`.
- Initial Module-26 verification: each `handle_connect()` completion emits one
  structured `info` trace event (`module="broker"`, `info="connect_handled"`).
- `ConnectResult.connack_properties` always contains at least
   `ReceiveMaximum` and `TopicAliasMaximum` from `BrokerConfig`.
- When `session_present == true`, `Broker` immediately flushes buffered offline
   messages for the client via `MessageRouter::flush_offline_queue()`.

#### Subscribe/Unsubscribe/Publish facades (19)

- `SubscriptionOrchestrator` (module `subscription_manager/`) encapsulates
   subscribe/unsubscribe protocol orchestration, including shared subscription
   parsing (`$share/<group>/<filter>`), per-filter validation, ACL checks,
   store/dispatcher updates, retained delivery, and protocol-error validation
   for invalid Subscription Identifier values.
- `SubscriptionOrchestrator` owns durable subscription snapshot
   synchronization for non-shared SUBSCRIBE/UNSUBSCRIBE outcomes.
- `handle_subscribe()` and `handle_unsubscribe()` in `Broker` delegate to
   `SubscriptionOrchestrator` under the broker mutex.
- `handle_publish()` wraps inbound publish routing and statistics increments under
   the broker mutex and returns MQTT reason codes for inbound QoS acknowledgements:
   `Success`, `NoMatchingSubscribers`, or `NotAuthorized`.

#### Housekeeping tick (22)

- `tick()` runs all periodic broker housekeeping under the broker mutex:
   - `WillPublisher::publish_due(now)` to emit delayed wills whose timers elapsed.
   - `SessionManager::cleanup_expired(now)` to remove expired sessions.
   - For each expired client ID, `WillPublisher::on_session_expired(client_id)`.
   - `SysTopicPublisher::tick(now)` for `$SYS` publication.
- The return value of `tick()` is the return value from `SysTopicPublisher::tick(now)`.
- Callers should invoke `tick()` repeatedly from the main loop and sleep for
   `BrokerConfig::tick_interval_ms` between iterations to avoid busy spinning.

#### Structured tracing configuration precedence (26.4)

Deterministic precedence is:

1. Defaults in `BrokerConfig`.
2. INI file (`ConfigLoader`).
3. CLI overrides in `main.cpp` (`--trace-level=...`, `--trace-module=...`).
4. Runtime system messages via `Broker::apply_trace_system_message()` delegated
   to monitoring command parsing.

Supported runtime topics:

- `$SYS/broker/tracing/global` with payload `none|error|warning|info|trace`
- `$SYS/broker/tracing/module/<module>` with payload `trace|none|on|off`

#### ConnectionManager integration (23)

- `Broker` owns one `std::unique_ptr<ConnectionManager>` created during
   `create_modules()`.
- The callback passed to `ConnectionManager` constructs a `ClientHandler` and
   calls `handler.run(conn, *this, config_, is_ws)`.
- Listener sockets and accept-loop threads are no longer direct `Broker`
   members.

---

## Constraints

- `startup()` / `shutdown()` should still be orchestrated from a single thread.
- Shared mutable runtime state is protected by component-local mutexes.
- Module accessors must not be called before `startup()`.
- `SessionPersistence`, `RetainedMessagePersistence`, and
   `InflightPersistence` are constructed during `startup()`; listener sockets
   are owned by `ConnectionManager` after `startup()`.
