# network/test — Unit Test Plan (Module 6)

All tests in `network_test.cpp`.  Catch2 tag: `[network]`.

---

## StreamBuffer (6.2)

| Test case | Behaviour |
|-----------|-----------|
| `stream_buffer_empty_on_construction` | `is_empty()` is true; `has_complete_packet()` is false |
| `stream_buffer_single_packet_1byte_rl` | Append a 3-byte packet (RL=1); consume returns it |
| `stream_buffer_single_packet_2byte_rl` | Append with 2-byte VBI RL; correctly detected |
| `stream_buffer_single_packet_3byte_rl` | Append with 3-byte VBI RL; correctly detected |
| `stream_buffer_single_packet_4byte_rl` | Append with 4-byte VBI RL; correctly detected |
| `stream_buffer_fragmented_delivery` | First half appended → no packet; second half → packet available |
| `stream_buffer_multiple_packets_in_one_append` | Two packets appended at once; both consumed |
| `stream_buffer_consume_without_complete_packet_throws` | `consume_packet()` on incomplete data → `std::logic_error` |
| `stream_buffer_zero_payload_packet` | Packet with RL=0 (PINGREQ); consumed correctly |

---

## WriteQueue (6.3)

| Test case | Behaviour |
|-----------|-----------|
| `write_queue_empty_on_construction` | `is_empty()` is true; `queued_bytes()` is 0 |
| `write_queue_enqueue_increments_bytes` | After enqueue, `queued_bytes()` equals packet size |
| `write_queue_backpressure_when_full` | Enqueue exceeding max_bytes returns false |
| `write_queue_drain_writes_to_connection` | Enqueue + drain; data appears on socket |
| `write_queue_drain_multiple_packets` | Multiple enqueued packets all written via drain |
| `write_queue_is_full_at_capacity` | `is_full()` returns true after capacity is reached |
| `write_queue_stop_exits_run_drain` | `stop()` causes `run_drain` thread to exit |
| `write_queue_run_drain_writes_enqueued_packets` | `run_drain` thread writes after `enqueue()` from another thread |

---

## TcpConnection (6.1.3)

| Test case | Behaviour |
|-----------|-----------|
| `tcp_connection_read_write_roundtrip` | write() on one end; read() on the other returns same bytes |
| `tcp_connection_is_open_after_construction` | `is_open()` is true for a valid fd |
| `tcp_connection_is_closed_after_close` | `is_open()` is false after `close()` |
| `tcp_connection_read_returns_zero_on_peer_close` | Peer closes → `read()` returns 0 |
| `tcp_connection_write_returns_false_on_closed_socket` | `write()` on closed socket returns `false` |
| `tcp_connection_move_transfers_ownership` | Move constructor; original is closed, new one is open |
| `tcp_connection_fd_returns_valid_descriptor` | `fd()` returns ≥ 0 for open connection |
| `tcp_connection_shutdown_socket_invalid_handle_noop` | `shutdown_socket(k_invalid_socket)` is a no-op |
| `tcp_connection_shutdown_socket_unblocks_peer_read` | `shutdown_socket(fd)` causes peer read to return 0 |

---

## TcpListener (6.1.1–6.1.2)

| Test case | Behaviour |
|-----------|-----------|
| `tcp_listener_opens_on_available_port` | `listen(0)` succeeds; `is_open()` is true |
| `tcp_listener_port_returns_bound_port` | `port()` returns non-zero after `listen(0)` |
| `tcp_listener_accept_returns_connection` | Client connects; `accept()` returns a non-null connection |
| `tcp_listener_close_marks_as_closed` | After `close()`, `is_open()` is false |
| `tcp_listener_move_transfers_ownership` | Move constructor; original is closed |
| `tcp_listener_ipv4_bind_failure_throws` | Binding to an already occupied IPv4 port throws `NetworkException` |
| `tcp_listener_ipv6_bind_failure_throws` | Binding to an already occupied IPv6 port throws `NetworkException` |
| `tcp_listener_socket_create_failure_throws` | Socket creation failure path throws `NetworkException` |
