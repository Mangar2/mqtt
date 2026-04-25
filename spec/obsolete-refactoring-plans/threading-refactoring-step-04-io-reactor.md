# Step 04 — IoReactor mit non-blocking Accept/Read/Write (Phase B, Bridge-Mode)

Bezug: `threading-refactoring.md` §5.1, §11, §12 Phase B

## Ziel

Accept- und Read/Write-Pfad laufen über einen einzelnen ereignisgetriebenen Reactor (kqueue auf macOS/BSD, epoll auf Linux). Die per-Connection-Threads bleiben **temporär** bestehen und übernehmen weiterhin Decode + Broker-Aufruf (Bridge-Mode). Nach diesem Schritt: Broker compileable, lauffähig, alle Tests grün, Reactor übernimmt I/O.

## Warum als viertes

- Setzt auf Step 01 (socket_ops, connection_slot, connection_table) auf.
- Setzt auf Step 03 (Facades, kein Broker-Lock) auf — sonst Race-Risiko.
- Liefert die Plattform-Schicht, die Step 05 für die Worker-Integration benötigt.
- Bridge-Mode hält Broker lauffähig, isoliert das Risiko der I/O-Umstellung von der Threading-Umstellung.

## Zu erstellende Dateien

| Datei | Zeilen Ziel | Inhalt |
|-------|-------------|--------|
| `src/network/io_reactor.h` | ~120 | Plattform-neutrale Schnittstelle. API: `start()`, `stop()`, `register_listener(int fd, AcceptCallback)`, `register_connection(int fd, ReadCallback, WriteCallback)`, `arm_write(int fd)`, `disarm_write(int fd)`, `unregister(int fd)`. Owns 1–2 Reactor-Threads. Internes `std::mutex` nur für Registrierungs-Änderungen. |
| `src/network/io_reactor_kqueue.cpp` | ~200 | macOS/BSD-Implementierung mit kqueue. EVFILT_READ / EVFILT_WRITE / EVFILT_EXCEPT. |
| `src/network/io_reactor_epoll.cpp` | ~200 | Linux-Implementierung mit epoll, EPOLLIN/EPOLLOUT/EPOLLRDHUP. |
| `cmake/network_platform.cmake` (oder Update vorhandener Datei) | — | Auswahl der Implementierungs-Quelle anhand `CMAKE_SYSTEM_NAME`. |
| `src/network/SPEC.md` | — | Update: Reactor-Architektur, Bridge-Mode. |

## Anpassungen an bestehender Konnektivität

- `ConnectionManager::start()`:
  - Erzeugt `IoReactor` und `ConnectionTable`.
  - Registriert Listener-Sockets beim Reactor mit Accept-Callback.
  - Accept-Callback: legt `ConnectionSlot` in `ConnectionTable` an, registriert die fd beim Reactor mit Read/Write-Callbacks, **startet wie bisher den per-Connection Client-Thread** (Bridge), der die Read-Daten aus dem Slot konsumiert.
- `ConnectionManager::stop()` stoppt zuerst den Reactor, dann die bestehenden Client-Threads (unverändert).
- `accept_loop` / `spawn_accept_loop` werden entfernt (ihre Aufgabe übernimmt der Reactor).
- Read-Callback des Reactors: `nb_read` in den `ConnectionSlot`-Read-Buffer, signalisiert dem Client-Thread per Condition-Variable im Slot (temporäre Brücke).
- Write-Callback: drainiert den `ConnectionSlot`-Write-Buffer per `nb_write`. Der Client-Thread füllt diesen Buffer statt direkt über `WriteQueue::run_drain()` zu schreiben.
- `WriteQueue::run_drain()` bleibt vorerst, wird in Step 05 entfernt.

## Unit-Tests

### `io_reactor.test.cpp`
- `start_then_stop_creates_and_joins_reactor_threads`.
- `register_listener_invokes_accept_callback_on_incoming_connection`.
- `register_connection_invokes_read_callback_when_data_arrives` — socketpair, Gegenseite schreibt, Callback wird aufgerufen.
- `arm_write_invokes_write_callback_when_socket_writable`.
- `unregister_stops_callbacks_for_fd`.
- `concurrent_register_unregister_does_not_drop_events` — Stress mit 100 fds.
- Plattform-bedingt: gleiche Test-Suite läuft sowohl gegen kqueue (auf macOS) als auch gegen epoll (auf Linux, falls CI verfügbar).

### Integration im Bridge-Mode
- Bestehende Integrationstests laufen unverändert grün — sie validieren, dass die End-to-End-Funktion erhalten bleibt.
- Neuer Integrationstest `io_reactor_accept_smoke`: Broker startet, 50 Clients connecten, Reactor signalisiert jede Verbindung, Client-Threads übernehmen Decode wie bisher.

## Akzeptanzkriterien

1. Broker startet, nimmt Clients an, akzeptiert Publish/Subscribe — über den Reactor.
2. Alle bestehenden Unit- und Integrationstests grün.
3. Neue `io_reactor`-Unit-Tests grün auf Host-Plattform.
4. `accept_loop` / `spawn_accept_loop` aus ConnectionManager entfernt.
5. Pro Verbindung weiterhin ein Client-Thread (Bridge) — explizit dokumentiert als temporär.
6. `io_reactor_kqueue.cpp` < 220 Zeilen, `io_reactor_epoll.cpp` < 220 Zeilen, `io_reactor.h` < 130 Zeilen.
7. CMake wählt korrekte Plattform-Implementierung automatisch.
8. Load-Test 18.1.2 läuft mindestens bis zur bisher erreichten Stage stabil durch.

## Out of Scope

- WorkerPool / JobScheduler werden noch nicht produktiv geschaltet.
- Per-Connection-Threads bleiben.
- `WriteQueue::run_drain()` bleibt — wird in Step 05 entfernt.
