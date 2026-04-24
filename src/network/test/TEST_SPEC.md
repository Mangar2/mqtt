# network/test — Unit Test Plan (Module 6)

Tests are split across:
- `network_test.cpp` (legacy coverage for `StreamBuffer`, `WriteQueue`,
  `TcpConnection`, `TcpListener`)
- `socket_ops_test.cpp`
- `connection_slot_test.cpp`
- `connection_table_test.cpp`
- `io_reactor_test.cpp`

Catch2 tag: `[network]`.

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

---

## SocketOps (Threading Refactor Step 01)

| Test case | Behaviour |
|-----------|-----------|
| `set_nonblocking_returns_ok_on_valid_fd` | `set_nonblocking()` succeeds and sets `O_NONBLOCK` |
| `nb_read_returns_would_block_on_empty_socket` | Non-blocking read on empty socket returns `WouldBlock` |
| `nb_read_returns_ok_with_bytes_when_data_present` | Read returns `Ok` and exact byte count when peer wrote data |
| `nb_read_returns_closed_on_peer_shutdown` | Read returns `Closed` after peer closes |
| `nb_write_returns_would_block_when_buffer_full` | Repeated writes on non-blocking socket eventually return `WouldBlock` |
| `nb_accept_returns_would_block_when_no_pending` | Non-blocking accept with no waiting client returns `WouldBlock` |
| `nb_accept_returns_ok_when_client_pending` | Non-blocking accept returns `Ok` and a valid accepted socket for pending client |

---

## ConnectionSlot (Threading Refactor Step 01)

| Test case | Behaviour |
|-----------|-----------|
| `slot_constructed_with_fd_starts_in_connecting_phase` | Slot starts in `Connecting` with stored fd |
| `slot_capacity_and_free_space_accessors_report_current_values` | Write capacity and free space values are reported correctly |
| `zero_capacity_write_capacity_is_clamped` | Write capacity 0 clamps to one-byte writable limit |
| `write_buffer_drains_in_fifo_order` | Write ring-buffer drains in FIFO order including wrap-around |
| `write_buffer_grows_and_trims_after_idle` | Write buffer grows for larger frames and later trims under idle low usage |
| `phase_transitions_connecting_connected_closing_are_legal` | Forward phase transitions are accepted |
| `phase_transition_to_same_phase_returns_true` | Transitioning to current phase is accepted as no-op |
| `phase_transition_back_to_connecting_is_rejected` | Backwards transition to `Connecting` is rejected |
| `phase_transition_from_closing_is_rejected` | Once closing, all further transitions are rejected |
| `move_constructor_transfers_fd_and_buffers` | Move construction transfers fd and buffered bytes |
| `move_assignment_transfers_fd_and_buffers` | Move assignment transfers state and invalidates source |
| `move_assignment_to_self_is_noop` | Self move-assignment leaves slot state unchanged |

---

## ConnectionTable (Threading Refactor Step 01)

| Test case | Behaviour |
|-----------|-----------|
| `add_then_find_returns_slot_pointer` | Added slot can be retrieved by fd |
| `add_duplicate_fd_returns_false` | Adding the same fd twice rejects the second insert |
| `find_unknown_fd_returns_nullptr` | Lookup of missing fd returns null |
| `const_find_returns_slot_pointer_for_existing_fd` | Const lookup returns the existing slot |
| `const_find_unknown_fd_returns_nullptr` | Const lookup of missing fd returns null |
| `remove_unregisters_and_destroys_slot` | Removed slot is no longer retrievable |
| `remove_unknown_fd_returns_false` | Removing missing fd reports false |
| `concurrent_find_from_many_threads_is_safe` | Multi-threaded lookups are safe and consistent |
| `concurrent_add_remove_is_safe` | Concurrent add/remove keeps table consistent |

---

## IoReactor (Threading Refactor Step 04)

| Test case | Behaviour |
|-----------|-----------|
| `start_then_stop_creates_and_joins_reactor_threads` | Reactor start/stop lifecycle is stable and idempotent |
| `register_listener_invokes_accept_callback_on_incoming_connection` | Listener callback runs when a client connects |
| `register_connection_invokes_read_callback_when_data_arrives` | Read callback runs after peer writes data |
| `arm_write_invokes_write_callback_when_socket_writable` | Write callback runs after write-interest is armed |
| `unregister_stops_callbacks_for_fd` | Unregistered fd no longer receives callbacks |
| `concurrent_register_unregister_does_not_drop_events` | Concurrent registration/unregistration remains stable under 100-fd stress |
