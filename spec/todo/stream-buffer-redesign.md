# StreamBuffer Redesign — Chunked Ring of Fixed-Size Segments

Status: proposal
Module: 6.2 (`src/network/stream_buffer.{h,cpp}`)

## 1. Problem with the current implementation

```cpp
class StreamBuffer {
  std::vector<uint8_t> buffer_;  // single contiguous vector
};
```

Operational issues observed under load:

1. **`consume_packet()` is O(n).**
   `buffer_.erase(begin, begin + n)` performs a `memmove` of every byte
   that follows the consumed packet. Under a backlog of hundreds of
   thousands of buffered bytes this is a per-packet O(buffered_size)
   cost — quadratic over a burst.
2. **`append()` may reallocate and copy the entire buffer.**
   Every growth past `capacity()` copies all retained bytes. A single
   slow consumer that lets the buffer grow to several MiB pays the
   full copy on every doubling.
3. **No upper bound.** A misbehaving or malicious client can cause
   unbounded heap growth.
4. **Memory never shrinks.** `vector::capacity()` only grows; once the
   buffer has spiked it stays large for the lifetime of the connection.
5. **`std::deque<uint8_t>` is not a fix.** Per-byte deque has poor
   cache behaviour, two-level indirection on every access, and the
   VBI/packet-size scan would walk node boundaries byte-by-byte.

## 2. Design goals

| Goal | Requirement |
|------|-------------|
| G1 | `append()` is amortised O(bytes_appended). No copy of already-buffered data. |
| G2 | `consume_packet()` is O(packet_size). Never moves bytes that remain in the buffer. |
| G3 | Memory grows on demand and **shrinks** as packets are drained. |
| G4 | Hard byte cap per connection. Exceeding it is a clean, reportable error — never an exception, never an OOM. |
| G5 | Allocation-light: hot path serves from a small per-buffer free list of equal-sized chunks. |
| G6 | Cache-friendly: bytes within a chunk are contiguous; the VBI parser and the packet-extraction copy work on `std::span`s. |
| G7 | No external synchronisation requirements changed (still single-threaded per connection). |

## 3. Data structure: ring of fixed-size chunks

```
            head_                                       tail_
              │                                           │
              ▼                                           ▼
   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
   │ chunk #k     │──▶│ chunk #k+1   │──▶│ chunk #k+2   │──▶ nullptr
   │ [r……w      ] │   │ [0……w      ] │   │ [0……w      ] │
   └──────────────┘   └──────────────┘   └──────────────┘
        │ read=r              │ read=0           │ read=0
        │ write=CAP           │ write=CAP        │ write=w
        └─ first unread byte  └─ full            └─ last partial
```

* A **chunk** is a fixed-size POD block (default `kChunkSize = 16 KiB`,
  configurable, must be ≥ 16 bytes).
* The buffer owns a singly-linked list of chunks, head → tail.
* Each chunk stores `read_pos` and `write_pos` as `uint16_t` (or
  `uint32_t` if larger chunks are configured).
* `append()` writes into `tail_`; when `tail_->write_pos == kChunkSize`
  a new chunk is taken from the free list (or freshly allocated) and
  appended.
* `consume_packet()` advances `head_->read_pos`. When
  `head_->read_pos == head_->write_pos` the chunk is unlinked and
  pushed onto the free list (G3, G5). The buffer never holds more than
  `kFreeListMax` empty chunks; surplus chunks are returned to the
  allocator (G3).

### 3.1 Why not `std::deque<uint8_t>`?

A deque’s segments are typically 16–512 bytes of `uint8_t` with one
indirection per element access. Our chunks are an order of magnitude
larger and store cursors, so:

* The VBI scan touches at most one or two chunks via raw pointer arithmetic.
* Packet extraction copies via `std::memcpy` over whole chunk slices,
  not byte-by-byte iterators.
* Chunk recycling is explicit — we control the high-water-mark, deque
  does not.

### 3.2 Why not a single growing ring buffer?

A single ring of size `N` works only while the total backlog stays
below `N`. We need the structure to grow past any preconfigured
size up to the hard cap (G3, G4) without reallocating a possibly
multi-megabyte block. The chunked list grows by adding a 16 KiB node;
no existing bytes move (G1).

## 4. Public API

The externally visible surface stays compatible except where the
caller currently relied on exceptions for backpressure.

```cpp
namespace mqtt {

struct StreamBufferConfig {
  std::size_t chunk_size      = 16 * 1024;   // bytes per chunk
  std::size_t max_buffered    = 1 * 1024 * 1024;  // hard cap (G4)
  std::size_t free_list_max   = 4;           // chunks kept for reuse (G5)
};

enum class StreamBufferAppendResult {
  kOk,
  kBufferFull,        // hard cap reached → caller must disconnect
};

class StreamBuffer {
public:
  explicit StreamBuffer(StreamBufferConfig cfg = {});
  ~StreamBuffer();

  StreamBuffer(const StreamBuffer&)            = delete;
  StreamBuffer& operator=(const StreamBuffer&) = delete;
  StreamBuffer(StreamBuffer&&) noexcept;
  StreamBuffer& operator=(StreamBuffer&&) noexcept;

  // G1, G4: append never throws, never reallocates existing data.
  [[nodiscard]] StreamBufferAppendResult append(std::span<const uint8_t> data) noexcept;

  [[nodiscard]] bool        has_complete_packet() const noexcept;
  [[nodiscard]] std::vector<uint8_t> consume_packet();   // G2

  [[nodiscard]] bool        is_empty() const noexcept;
  [[nodiscard]] std::size_t size()     const noexcept;   // bytes currently buffered
  [[nodiscard]] std::size_t capacity() const noexcept;   // bytes currently allocated
};

} // namespace mqtt
```

### 4.1 Behavioural contract

* `append()` returns `kBufferFull` **before** writing any byte if the
  incoming span would push `size() > cfg.max_buffered`. The buffer
  state is unchanged on rejection. The caller (the connection layer)
  is responsible for the protocol-level reaction (4.3).
* `consume_packet()` keeps its existing precondition
  (`has_complete_packet() == true`) and existing throw contract
  (`std::logic_error` on misuse).
* `front_packet_size()` becomes a private free function operating on
  a small VBI peek (≤ 5 bytes) extracted from the head chunk(s).

## 5. Hot-path algorithms

### 5.1 `append`

```text
remaining := data.size()
if size() + remaining > max_buffered: return kBufferFull
while remaining > 0:
    if tail_ == nullptr or tail_->write_pos == chunk_size:
        tail_ = acquire_chunk()              // free list or new
    n := min(remaining, chunk_size - tail_->write_pos)
    memcpy(tail_->data + tail_->write_pos, src, n)
    tail_->write_pos += n
    src += n; remaining -= n
return kOk
```

Cost: one `memcpy` per spanned chunk, no relocation of buffered bytes.

### 5.2 `front_packet_size` (VBI peek)

The MQTT fixed header is at most 5 bytes (1 type byte + ≤ 4 VBI). We
do **not** require those 5 bytes to be contiguous in memory; instead:

```text
peek up to 5 bytes from head into a stack array,
walking head_ and head_->next as needed
```

This runs in constant time and bounded chunk crossings (at most 2).

### 5.3 `consume_packet`

```text
total := front_packet_size()
out := vector<uint8_t>(); out.reserve(total)
remaining := total
while remaining > 0:
    avail := head_->write_pos - head_->read_pos
    n := min(remaining, avail)
    out.append(head_->data + head_->read_pos, n)
    head_->read_pos += n
    remaining -= n
    if head_->read_pos == head_->write_pos:
        old := head_; head_ = head_->next
        release_chunk(old)            // free list, or free()
        if head_ == nullptr: tail_ = nullptr
return out
```

* No bytes following the consumed packet are touched (G2).
* Drained chunks are released immediately, so `capacity()` tracks the
  actual high-water mark of the **current** backlog (G3).
* The single copy into the returned vector is unavoidable — the codec
  consumes contiguous bytes. It is bounded by the packet size, not by
  the buffer size.

### 5.4 Free list

* `release_chunk()`: if free-list size < `cfg.free_list_max`, push;
  otherwise `delete`.
* `acquire_chunk()`: pop from free list, else `new` a chunk.
* The free list is a single-linked LIFO — O(1), no allocations on the
  steady-state path of a chatty connection (G5).

## 6. Hard cap and protocol reaction (G4)

When `append()` returns `kBufferFull`:

1. The connection layer logs a single
   `stream_buffer_overflow{client_id, max_buffered, attempted}` event
   (subject to anti-storm throttling).
2. For MQTT 5: send `DISCONNECT` with reason code
   `0x95 Packet too large` (if the in-flight packet header indicated a
   size > cap) or `0x97 Quota exceeded` (steady-stream backlog).
3. For MQTT 3.1.1: close the TCP connection without DISCONNECT.
4. Tear down the session through the normal path
   (`ConnectionRegistry::unregister`).

The cap is **per connection**; default 1 MiB is large enough for any
legitimate single MQTT 5 packet (Maximum Packet Size negotiated at
CONNECT) yet small enough to bound worst-case heap usage by
`max_buffered × max_concurrent_connections`.

The Maximum Packet Size advertised in CONNACK should be set to
`min(broker_default, cfg.max_buffered)` so that compliant clients
never trigger the overflow path for a single packet.

## 7. Memory budget worst case

```
worst_case_per_client = chunks_in_use * chunk_size
                     ≤ ceil(max_buffered / chunk_size) * chunk_size
                     + free_list_max * chunk_size
```

With defaults (chunk = 16 KiB, max = 1 MiB, free_list = 4):
`64 + 4 = 68` chunks → **1.0625 MiB per stalled connection**, hard
upper bound. With 10 000 simultaneous slow clients this is ~10.6 GiB
in the absolute worst case; in normal operation a streaming client
holds 1–2 chunks (16–32 KiB).

## 8. Testing strategy

Unit tests (no production C++ reads required for the test author):

| ID | Scenario | Assertion |
|----|----------|-----------|
| T1 | append small fragments, consume one packet | bytes returned identical to input |
| T2 | packet split across 3 chunks | `consume_packet()` reassembles correctly |
| T3 | VBI split across chunk boundary | `has_complete_packet()` becomes true on the byte that completes the VBI |
| T4 | append 100 000 small packets, consume in lock-step | `capacity()` stays bounded by `2 * chunk_size` |
| T5 | append 100 000 small packets without consuming | `append()` returns `kBufferFull` exactly when `size() > max_buffered` and leaves `size()` unchanged |
| T6 | drain to empty, then append again | free list reused, no fresh allocation (verify via custom allocator hook) |
| T7 | malformed VBI (5th byte still has continuation bit) | `consume_packet()` returns the 6-byte malformed frame so the codec can reject it (existing behaviour preserved) |
| T8 | move-construct/move-assign | source becomes empty, destination owns the chunks |

Performance micro-benchmark (`tests/perf/stream_buffer_bench.cpp`,
opt-in): append+consume 1 M × 256-byte packets must complete in a
fraction of a second and allocate ≤ `free_list_max + 1` chunks total
on the steady state.

## 9. Migration plan

1. Land the new implementation behind the same header; only `append()`
   signature changes (`void` → `[[nodiscard]] StreamBufferAppendResult`).
2. Update the single call site in
   `src/connection/connection.cpp` (or wherever `append()` is invoked)
   to handle `kBufferFull` per §6.
3. Update existing unit tests under `tests/network/` to call
   `(void)buf.append(...)` where the result is irrelevant.
4. Add the new tests T4–T8.
5. Remove the old single-vector implementation.

No public API change reaches the codec, the message router, or the
session layer — the redesign is local to module 6.2.

## 10. Non-goals

* Zero-copy delivery to the codec (would require rewriting the codec
  to consume `std::span` lists; out of scope for this change).
* Lock-free multi-producer access (the buffer remains
  single-threaded per connection, see G7).
* Adaptive chunk sizing (constant size keeps the free list trivial;
  revisit only if profiling shows fragmentation).
