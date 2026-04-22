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
| `write_queue.h/.cpp`   | 6.3      | `WriteQueue` — thread-safe outgoing packet queue with optional sink flush |
| `socket_ops.h/.cpp`    | step 01  | Non-blocking socket helpers (`set_nonblocking`, `nb_read`, `nb_write`, `nb_accept`) |
| `connection_slot.h/.cpp` | step 01 | Per-connection fd + read/write ring buffers + phase (`Connecting`, `Connected`, `Closing`) |
| `connection_table.h/.cpp`| step 01, step 05 | Thread-safe fd-indexed ownership table for `Entry { ConnectionSlot, ConnectionSession }` |
| `io_reactor.h`         | step 04  | Platform-neutral reactor interface and callback API |
| `io_reactor_kqueue.cpp`| step 04  | kqueue backend (`EVFILT_READ`, `EVFILT_WRITE`) for macOS/BSD |
| `io_reactor_epoll.cpp` | step 04  | epoll backend (`EPOLLIN`, `EPOLLOUT`, `EPOLLRDHUP`) for Linux |

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
| `shutdown_socket(SocketHandle)` | Static helper: shutdown an externally-owned socket handle |

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

**Purpose:** Thread-safe queue of outgoing serialized packets with optional
immediate sink flushing.

**Public API:**

| Method | Description |
|--------|-------------|
| `WriteQueue(max_bytes)` | Create queue with capacity limit (default 64 KiB) |
| `set_sink(writer)` | Optional sink callback for immediate flush behavior |
| `enqueue(vector<uint8_t>)` → `bool` | Add packet; false = queue full (backpressure) |
| `drain(TcpConnection&)` | Blocking: write all queued packets then return |
| `stop()` | Mark queue stopped and reject future enqueue calls |
| `is_full()` → `bool` | True when queued bytes ≥ max_bytes |
| `is_empty()` → `bool` | True when queue holds no packets |
| `queued_bytes()` → `size_t` | Current total byte count in the queue |

**Constraints:**
- `enqueue`, `set_sink`, and `stop` are safe to call from different threads.
- Backpressure: `enqueue` returns `false` (does not throw) when at capacity.

---

### `SocketOps` (threading-refactoring step 01)

**Purpose:** Provide non-blocking one-shot helpers around socket syscalls.

**Public API:**

| Function | Description |
|----------|-------------|
| `set_nonblocking(fd)` | Sets socket into non-blocking mode |
| `nb_read(fd, span, out_bytes)` | One non-blocking `recv`; returns `IoResult` |
| `nb_write(fd, span, out_bytes)` | One non-blocking `send`; returns `IoResult` |
| `nb_accept(listen_fd, out_fd)` | One non-blocking `accept`; returns `IoResult` |

`IoResult` values:
- `Ok`: operation completed; out parameter contains produced count/handle.
- `WouldBlock`: `EAGAIN`/`EWOULDBLOCK`.
- `Closed`: peer closed connection (read/write path).
- `Error`: non-recoverable socket failure.

---

### `ConnectionSlot` (threading-refactoring step 01)

**Purpose:** Hold per-connection I/O state without thread ownership.

Contains:
- socket handle (`fd`)
- read ring buffer
- write ring buffer
- phase enum: `Connecting` → `Connected` → `Closing`

Constraints:
- no internal locks
- copy disabled, move enabled
- buffer push rejects data when capacity would be exceeded

---

### `ConnectionTable` (threading-refactoring step 01 + step 05)

**Purpose:** Own per-fd connection entries and provide fd-indexed access.

Entry model:
- `Entry { ConnectionSlot slot; std::unique_ptr<ConnectionSession> session; }`

Public API:
- `add(fd, slot, session)` inserts an entry by fd and returns `false` if fd already exists.
- `remove(fd)` removes an entry and returns whether removal happened.
- `find(fd)` returns entry pointer or `nullptr`.
- `clear()` removes all entries.

Locking:
- one internal `std::shared_mutex`
- shared-lock lookup (`find`)
- unique-lock mutation (`add`, `remove`)

---

### `IoReactor` (threading-refactoring step 04)

**Purpose:** Central event loop for non-blocking listener and connection events.

Public API:
- `start()` / `stop()` control the reactor thread.
- `register_listener(fd, accept_callback)` registers listener accept readiness.
- `register_connection(fd, read_callback, write_callback)` registers socket
  read/write readiness callbacks.
- `arm_write(fd)` / `disarm_write(fd)` toggle write-interest.
- `unregister(fd)` removes all events for one socket.

Platform mapping:
- macOS/BSD: `io_reactor_kqueue.cpp`
- Linux: `io_reactor_epoll.cpp`

Locking:
- one internal `std::mutex` for registration mutations
- callback dispatch runs on the reactor thread

---

## `test/` — Unit tests

See `test/TEST_SPEC.md` for the full test plan.
