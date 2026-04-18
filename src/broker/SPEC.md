# broker — Broker Orchestrator + Concurrency + Connect Facade (Modules 15, 17, 18)

Wires all modules together, controls broker startup / shutdown, and provides
thread-safe access to shared broker state.
Depends on: all previous modules (1–14), plus shared-state coordination for Module 17.

---

## Responsibilities

- Load and validate broker configuration from a text file (INI format).
- Instantiate every module object and inject its dependencies.
- Bind persistence adapters to in-memory stores on startup.
- Open TCP listeners for MQTT and (optionally) WebSocket connections.
- Install SIGTERM / SIGINT signal handlers.
- Perform ordered startup and ordered shutdown.
- Track active connections so the message router can deliver to online clients.
- Guard all shared mutable broker state with one broker-level mutex.
- Provide thread-safe wrappers for session connect / disconnect / connection-loss paths.
- Provide a high-level connect facade that returns full handshake outcome data.

---

## Files

| File | Plan ref | Description |
|------|----------|-------------|
| `broker_error.h`    | 15   | `BrokerError` enum and `BrokerException` |
| `broker_config.h`   | 15.1 | `BrokerConfig` struct with all configuration parameters |
| `config_loader.h/.cpp` | 15.1.1 | `ConfigLoader` — INI-file parser → `BrokerConfig` |
| `broker.h/.cpp`     | 15.2–15.3, 17, 18 | `Broker` — component wiring, startup/shutdown, signal handling, broker-level locking, connect facade |

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
| `[broker]`    | `session_expiry_max`  | uint32   | `0`     | Hard cap on session expiry seconds. `0` = unlimited. |
| `[broker]`    | `topic_alias_maximum` | uint16   | `10`    | Maximum topic alias value per connection. |
| `[broker]`    | `max_queued_messages` | uint32   | `100`   | Per-client offline queue capacity. |
| `[persistence]` | `enabled`           | bool     | `false` | Enable crash-safe file persistence. |
| `[persistence]` | `dir`               | string   | `./data`| Directory for persistence snapshot files. |

### Validation rules

- `mqtt_port` and `ws_port`: `0` disables the listener; `1–65535` are valid non-zero values.  
  At least one of `mqtt_port` or `ws_port` must be non-zero (enforced by `ConfigLoader`).
- `max_connections`: `1–100 000`.
- `receive_maximum`: `1–65535`.
- `topic_alias_maximum`: `0–65535`. `0` disables topic aliases.
- `max_queued_messages`: `1–100 000`.
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
    uint32_t session_expiry_max    = 0;
    uint16_t topic_alias_maximum   = 10;
    uint32_t max_queued_messages   = 100;
    bool     persistence_enabled   = false;
    std::filesystem::path persistence_dir = "./data";
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

struct ConnectResult {
   bool session_present;
   ReasonCode reason_code;
   std::vector<Property> connack_properties;
   std::string client_id;
};

// Thread-safe connect/disconnect facades (Modules 17.3 + 18)
ConnectResult handle_connect(const ConnectPacket& connect_packet,
                             std::function<void()> close_callback);
void handle_disconnect(std::string_view client_id, ReasonCode reason_code,
                       std::optional<uint32_t> expiry_override,
                       std::chrono::steady_clock::time_point now);

void handle_connection_lost(std::string_view client_id,
                            std::chrono::steady_clock::time_point now);

// Connection registration — call from connection handler
using SendFn = std::function<void(const Message&)>;
void register_connection(std::string_view client_id, SendFn send_fn);
void unregister_connection(std::string_view client_id) noexcept;

// 15.3.3 signal handling
static void install_signal_handlers() noexcept;
[[nodiscard]] static bool shutdown_requested() noexcept;
```

#### Startup sequence (15.3.1)

1. Throw `BrokerException(AlreadyRunning)` if already running.
2. Instantiate all module objects in dependency order.
3. Configure authenticator according to `allow_anonymous`.
4. Load ACL: add a broker-internal "allow all" rule so that will messages
   routed by the system can pass publish authorisation.
5. If `persistence_enabled`: call `load_persistence()` to populate in-memory
   stores from the snapshot files.
6. Open `TcpListener` on `mqtt_port` (if non-zero).
7. Set `running_ = true`.

#### Shutdown sequence (15.3.2)

1. Return immediately if not running.
2. Close TCP listeners (accept loops will receive an error and exit).
3. If `persistence_enabled`: call `flush_persistence()` to write current
   store contents to snapshot files.
4. Set `running_ = false`.

#### Will publish callback

Will messages published by `WillPublisher` are routed via `MessageRouter::route()`
using a reserved client identifier `"_broker_will_system_"`.  An unconditional
ACL allow-all rule is registered for this principal during startup so that
will messages always pass publish authorisation.

#### Signal handling (15.3.3)

`install_signal_handlers()` registers a C signal handler for both `SIGTERM`
and `SIGINT` that sets the internal `shutdown_requested_` atomic flag to `true`.
Callers should poll `Broker::shutdown_requested()` in their main loop and call
`shutdown()` when it returns `true`.

#### Concurrency layer (17)

- `Broker` owns a `std::shared_mutex` and acquires an exclusive lock around
   `register_connection()`, `unregister_connection()`, `route_message()`, and `tick()`.
- `handle_connect()` wraps `SessionManager::handle_connect()` under the same lock.
- `handle_disconnect()` wraps will handling, connection unregister, and
   `SessionManager::handle_disconnect()` under one lock.
- `handle_connection_lost()` wraps will handling, connection unregister, and
   session teardown under one lock.
- Direct public accessors to internal mutable stores are intentionally removed
   from `Broker` to prevent unguarded external mutation.

#### Connect facade (18)

- `handle_connect()` authenticates CONNECT first and returns auth failure as
   `ConnectResult.reason_code`.
- On authentication success, it delegates session open/resume to
   `SessionManager::handle_connect()`.
- If CONNECT contains a Will, `Broker` extracts `WillMessage` internally and
   stores it via `WillPublisher::on_connect()`.
- `ConnectResult.connack_properties` always contains at least
   `ReceiveMaximum` and `TopicAliasMaximum` from `BrokerConfig`.
- When `session_present == true`, `Broker` immediately flushes buffered offline
   messages for the client via `MessageRouter::flush_offline_queue()`.

---

## Constraints

- `startup()` / `shutdown()` should still be orchestrated from a single thread.
- Shared mutable runtime state is protected by the broker mutex.
- Module accessors must not be called before `startup()`.
- `TcpListener`, `SessionPersistence`, `RetainedMessagePersistence`, and
  `InflightPersistence` are constructed during `startup()`; the class does
  not hold any network handles before that call.
