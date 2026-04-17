# network — Network Layer (Module 6)

Raw TCP communication layer. Has no MQTT knowledge. Hosts no application logic.
Depends on: nothing (no other project modules).

---

## Purpose

Provides the OS-level primitives to open a listening socket, accept TCP
connections, buffer incoming byte streams, and queue outgoing byte sequences.
Higher modules (Connection Handler, Module 7) use this layer exclusively for
all I/O.

---

## Files

| File                   | Plan ref | Description |
|------------------------|----------|-------------|
| `network_error.h`      | 6        | `NetworkError` enum and `NetworkException` |
| `tcp_connection.h/.cpp`| 6.1.3    | `TcpConnection` — owns a connected socket fd; read/write/close |
| `tcp_listener.h/.cpp`  | 6.1.1–2  | `TcpListener` — opens server socket, runs accept loop |
| `stream_buffer.h`      | 6.2      | `StreamBuffer` — buffers incoming bytes, extracts complete MQTT packets |
| `write_queue.h/.cpp`   | 6.3      | `WriteQueue` — thread-safe outgoing packet queue with async drain |

---

## Component Details

### `NetworkError` / `NetworkException`

Error codes for socket and I/O failures:

| Code | Meaning |
|------|---------|
| `BindFailed`        | `bind()` call failed |
| `ListenFailed`      | `listen()` call failed |
| `AcceptFailed`      | `accept()` call failed |
| `SocketCreateFailed`| `socket()` call failed |
| `SetSockOptFailed`  | `setsockopt()` call failed |
| `WriteFailed`       | Send to socket failed |
| `ReadFailed`        | Receive from socket failed |
| `QueueFull`         | Write queue capacity exceeded (backpressure) |

---

### `TcpConnection` (6.1.3)

**Purpose:** Owns a single connected socket file descriptor.

**Public API:**

| Method | Description |
|--------|-------------|
| `TcpConnection(int fd)` | Takes ownership of an already-connected fd |
| `read(span<uint8_t>)` → `ssize_t` | Blocking receive; 0 on peer close, -1 on error |
| `write(span<const uint8_t>)` → `bool` | Blocking send of all bytes; false on error |
| `close()` | Shutdown and close the socket |
| `is_open()` → `bool` | True while the fd is valid and not closed |
| `fd()` → `int` | Access the underlying fd (for tests / multiplexing) |

**Constraints:**
- Non-copyable; movable.
- Destructor closes the fd if still open.

---

### `TcpListener` (6.1.1–6.1.2)

**Purpose:** Opens a TCP server socket and blocks on `accept()`.

**Public API:**

| Method | Description |
|--------|-------------|
| `static listen(port, ipv6)` → `TcpListener` | Creates and binds socket; throws on failure |
| `accept()` → `unique_ptr<TcpConnection>` | Blocking accept; returns new connection |
| `close()` | Close the server socket |
| `is_open()` → `bool` | True while the fd is valid |

**Constraints:**
- `SO_REUSEADDR` always set.
- IPv6 socket uses `IPV6_V6ONLY = 0` to also accept IPv4 clients.
- Non-copyable; movable.

---

### `StreamBuffer` (6.2)

**Purpose:** Incrementally accumulates raw bytes from a TCP stream.  
Detects complete MQTT packets by reading the fixed-header Remaining Length
field (variable-byte-integer at offset 1–4) and returns them one at a time.

**Public API:**

| Method | Description |
|--------|-------------|
| `append(span<const uint8_t>)` | Add bytes received from socket |
| `has_complete_packet()` → `bool` | True if ≥ 1 complete packet is buffered |
| `consume_packet()` → `vector<uint8_t>` | Remove and return the front packet |
| `is_empty()` → `bool` | True when the internal buffer holds no bytes |

**Packet framing logic:**
1. The MQTT fixed header is at least 2 bytes: `[type+flags][remaining_length…]`.
2. The Remaining Length is a variable-byte integer at byte offset 1; its
   encoded size is 1–4 bytes (MSB continuation bit).
3. Total packet size = (index of first RL byte after header) + remaining_length.
4. If fewer bytes than the total size are available, no packet is returned.

---

### `WriteQueue` (6.3)

**Purpose:** Thread-safe queue of outgoing serialized packets.  
A background drain loop writes them to the `TcpConnection`.

**Public API:**

| Method | Description |
|--------|-------------|
| `WriteQueue(max_bytes)` | Create queue with capacity limit (default 64 KiB) |
| `enqueue(vector<uint8_t>)` → `bool` | Add packet; false = queue full (backpressure) |
| `drain(TcpConnection&)` | Blocking: write all queued packets then return |
| `run_drain(TcpConnection&)` | Loop: wait for packets, drain, repeat until stopped |
| `stop()` | Signal the drain loop to exit |
| `is_full()` → `bool` | True when queued bytes ≥ max_bytes |
| `is_empty()` → `bool` | True when queue holds no packets |
| `queued_bytes()` → `size_t` | Current total byte count in the queue |

**Constraints:**
- `enqueue` and `run_drain` are safe to call from different threads.
- `run_drain` blocks the calling thread; intended to run in a `std::jthread`.
- Backpressure: `enqueue` returns `false` (does not throw) when at capacity.

---

## `test/` — Unit tests

See `test/TEST_SPEC.md` for the full test plan.
