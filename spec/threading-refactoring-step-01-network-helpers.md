# Step 01 — Network Helper Classes (standalone)

Bezug: `threading-refactoring.md` §5.1, §11, §14

## Ziel

Reine Hilfsklassen für non-blocking I/O bauen. Keine Integration in Broker / ConnectionManager. Nach diesem Schritt: Broker unverändert, lauffähig, compileable. Neue Klassen sind nur via Unit-Tests aktiv.

## Warum zuerst

- Pure Funktionen / Container, ohne Threading-Berührung.
- Liefern Bausteine für Step 04 (IoReactor) und Step 05 (Worker-Integration).
- Risiko isoliert: kein Produktionscode wird angefasst.

## Zu erstellende Dateien

| Datei | Zeilen Ziel | Inhalt |
|-------|-------------|--------|
| `src/network/socket_ops.h` | ~40 | API: `set_nonblocking(fd)`, `nb_read(fd, span)`, `nb_write(fd, span)`, `nb_accept(listen_fd)`. Rückgabe-Enum `IoResult { Ok, WouldBlock, Closed, Error }`. Pure Funktionen, kein State, kein Lock. |
| `src/network/socket_ops.cpp` | ~60 | POSIX Implementierung (fcntl, recv, send, accept4/accept). Plattform-Guards für `EAGAIN`/`EWOULDBLOCK`. |
| `src/network/connection_slot.h` | ~80 | Per-Connection I/O-State: fd, read-Ringbuffer, write-Ringbuffer, Phase-Flag (`Connecting`, `Connected`, `Closing`). Konstruktor übernimmt fd. Verbietet copy, erlaubt move. Kein Lock — Zugriff erfolgt später über JobScheduler-Serialisierung. |
| `src/network/connection_slot.cpp` | ~100 | Buffer-Verwaltung (push/pop bytes, contiguous-view), Phase-Übergänge. |
| `src/network/connection_table.h` | ~60 | Owns `ConnectionSlot`-Instanzen, Index nach fd. API: `add(slot)`, `remove(fd)`, `find(fd) -> ConnectionSlot*`. Internes `std::shared_mutex`. |
| `src/network/connection_table.cpp` | ~80 | `unordered_map<int, unique_ptr<ConnectionSlot>>`, shared-lock für find, unique-lock für add/remove. |
| `src/network/SPEC.md` | — | Update: neue Dateien dokumentieren. |

## Unit-Tests (co-located neben jeder .cpp)

### `socket_ops.test.cpp`
- `set_nonblocking_returns_ok_on_valid_fd` — socketpair anlegen, set_nonblocking, fcntl prüft `O_NONBLOCK`.
- `nb_read_returns_would_block_on_empty_socket` — non-blocking pipe lesen ohne Daten.
- `nb_read_returns_ok_with_bytes_when_data_present` — write von Gegenseite, dann nb_read.
- `nb_read_returns_closed_on_peer_shutdown` — Gegenseite schließt, nb_read meldet Closed.
- `nb_write_returns_would_block_when_buffer_full` — kleinen SO_SNDBUF setzen, in Schleife schreiben bis WouldBlock.
- `nb_accept_returns_would_block_when_no_pending` — listen-socket non-blocking, accept ohne client.

### `connection_slot.test.cpp`
- `slot_constructed_with_fd_starts_in_connecting_phase`.
- `read_buffer_push_pop_preserves_bytes`.
- `read_buffer_full_rejects_push_returns_false`.
- `write_buffer_drains_in_fifo_order`.
- `phase_transitions_connecting_connected_closing_are_legal`.
- `phase_transition_back_to_connecting_is_rejected`.
- `move_constructor_transfers_fd_and_buffers`.

### `connection_table.test.cpp`
- `add_then_find_returns_slot_pointer`.
- `find_unknown_fd_returns_nullptr`.
- `remove_unregisters_and_destroys_slot`.
- `concurrent_find_from_many_threads_is_safe` — 8 Threads, jeweils 10k Lookups, Sanitizer sauber.
- `concurrent_add_remove_is_safe` — 4 Threads add, 4 Threads remove, Map konsistent.

## Akzeptanzkriterien

1. Alle neuen Dateien compileren in der bestehenden CMake-Struktur (Eintrag in `src/network/CMakeLists.txt` o. ä.).
2. Alle Unit-Tests grün via `python test/run_coverage.py`.
3. Bestehende Unit- und Integrationstests unverändert grün.
4. Broker- und ConnectionManager-Quellen **unverändert**.
5. `socket_ops.cpp` < 80 Zeilen, `connection_slot.cpp` < 120 Zeilen, `connection_table.cpp` < 100 Zeilen.
6. Kein Lock in `socket_ops`, `connection_slot`. Genau ein `shared_mutex` in `connection_table`.

## Out of Scope

- Keine Verwendung in Broker / ConnectionManager.
- Kein IoReactor.
- Keine Plattform-Implementierung außer POSIX (kqueue/epoll erst Step 04).
