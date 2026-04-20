# Step 05 — Worker-Pool Integration & Cleanup (Phase C + D)

Bezug: `threading-refactoring.md` §5.2, §5.4, §6, §7, §12 Phase C+D, §15

## Ziel

Per-Connection-Threads vollständig entfernen. Der WorkerPool aus Step 02 verarbeitet alle Decode-, Drain- und Close-Jobs. Der IoReactor aus Step 04 gibt nur noch Jobs in den Scheduler. ConnectionManager schrumpft auf einen dünnen Owner. `WriteQueue::run_drain` und der Drain-Thread verschwinden. Nach diesem Schritt: Broker compileable, lauffähig, alle Tests grün, Thread-Anzahl bei 3200 Verbindungen ≤ `max_threads + 3`.

## Warum als letztes

- Benötigt alle vorherigen Bausteine: Hilfsklassen (Step 01), Worker-Pool (Step 02), Facades ohne Broker-Lock (Step 03), Reactor (Step 04).
- Risikoreichster Schritt — wird auf eine bewährte Basis gesetzt.
- Erfüllt die Erfolgskriterien aus §15.

## Zu erstellende / zu ändernde Dateien

| Datei | Aktion | Zeilen Ziel |
|-------|--------|-------------|
| `src/connection/connection_session.h/.cpp` | NEU. Per-Connection Heap-Zustand: `ClientSession`, `WriteQueue`, `StreamBuffer`, `TopicAliasTable`, `ConnectResult`. Eigentum bei `ConnectionSlot` (oder ConnectionTable), kein Lock — Zugriff serialisiert via `JobScheduler`. | ~80h / ~60cpp |
| `src/connection/client_handler.h/.cpp` | UMSCHREIBEN auf zustandslosen Job-Prozessor. API: `process_decode_job(ConnectionSlot&, ConnectionSession&, Broker&)`, `process_drain_job(ConnectionSlot&, ConnectionSession&)`, `process_close_job(ConnectionSlot&, ConnectionSession&, Broker&)`. Kein Thread, kein Lock. | bleibt klein, ändert Form |
| `src/connection/connection_manager.h/.cpp` | SCHRUMPFEN. Owns `IoReactor`, `WorkerPool`, `JobScheduler`, `ConnectionTable`. `start()` startet alle. `stop()` stoppt alle. Keine `client_threads_`, kein `cleanup_finished`, kein `join_all_clients`. Kein Lock. | ~50h / ~60cpp |
| `src/network/write_queue.h/.cpp` | ÄNDERN: `run_drain()` und Drain-Thread entfernen. WriteQueue wird zu reiner Datenstruktur, von Workern befüllt, vom Reactor geleert. | schrumpft |
| `src/network/io_reactor*.cpp` | Anpassen: Read/Write-Callbacks erzeugen jetzt `ConnectionJob` und reichen sie an `JobScheduler::submit` weiter — kein Aufruf des Client-Threads mehr. | minimal |
| `src/connection/SPEC.md`, `src/executor/SPEC.md`, `src/network/SPEC.md` | Updates. | — |

## Anpassungen am Datenfluss

Inbound:
1. Reactor liest Bytes in `ConnectionSlot.read_buffer`.
2. Reactor erzeugt `DecodeJob{fd}` und ruft `JobScheduler::submit`.
3. Scheduler stellt sicher: max. ein aktiver Job pro fd. Sonst Backlog.
4. Worker zieht Job aus JobQueue, ruft `client_handler::process_decode_job(slot, session, broker)`.
5. Worker schreibt Antworten in `slot.write_buffer`, ruft `reactor.arm_write(fd)`.
6. Worker meldet `JobScheduler::mark_done(fd)`.

Outbound:
1. Broker-Facade pusht in `OutboundQueue` der Session.
2. Push erzeugt `DrainJob{fd}` über `JobScheduler::submit`.
3. Worker drainiert in `slot.write_buffer`, armed write.
4. Reactor sendet bytes non-blocking.

Close:
1. Reactor bemerkt Peer-Close (`IoResult::Closed`) → `CloseJob`.
2. Worker führt `process_close_job` aus (Will, Session-Cleanup, Unregister via DisconnectFacade).
3. ConnectionTable::remove(fd).

## Unit-Tests

### `connection_session.test.cpp`
- `session_constructed_with_client_id_owns_subobjects`.
- `topic_alias_table_round_trip`.
- `write_queue_appended_then_drained_via_buffer`.

### `client_handler.test.cpp` (komplette Neufassung)
- `process_decode_job_consumes_one_frame_and_invokes_broker_facade`.
- `process_decode_job_handles_partial_frame_and_returns_without_call`.
- `process_drain_job_moves_outbound_messages_to_write_buffer`.
- `process_close_job_invokes_disconnect_facade_and_removes_session`.
- `processor_holds_no_mutex_and_is_reentrant_safe_per_distinct_fd`.

### `connection_manager.test.cpp` (Update)
- `start_initializes_reactor_pool_scheduler_table`.
- `stop_orderly_shuts_down_reactor_then_pool_then_table`.
- `connection_manager_holds_no_mutex_field` (statisch / Konvention).
- `no_thread_started_per_connection` — 100 Verbindungen, Thread-Anzahl bleibt ≤ pool_max + reactor_threads + 1.

### Integration
- Alle bestehenden Integrationstests grün.
- Load-Test **18.1.2 stage 3200 läuft im Timeout durch** (§15.8).
- Neuer Integrationstest `bounded_thread_count_under_load`: 3200 parallele Clients, Process-Thread-Count zu jedem Zeitpunkt ≤ `max_threads + 3`.

## Akzeptanzkriterien (= §15 Erfolgskriterien)

1. **Thread-Count bounded:** ≤ `max_threads + 2 (Reactor) + 1 (main)`, unabhängig von Verbindungszahl.
2. **Kein `std::thread::join()` im Hot-Path** — nur in `stop()`.
3. **Broker.h < 350, Broker.cpp < 165** (bereits in Step 03 erreicht, bleibt erhalten).
4. **ConnectionManager.h < 100, ConnectionManager.cpp < 80**.
5. **Null Mutexe in Broker, ConnectionManager, ClientHandler, ConnectionSlot, ConnectionJob, PoolScalingPolicy, SocketOps, BrokerModuleFactory, PersistenceCoordinator** (§8).
6. Alle Unit-Tests grün.
7. Alle Integrationstests grün, inkl. Load 18.1.2 alle Stages.
8. `WriteQueue::run_drain` und Drain-Thread entfernt.
9. `client_threads_` Vector + Cleanup-Logik aus ConnectionManager entfernt.
10. ThreadSanitizer-Lauf mit Smoke-Last (z. B. 200 Clients) sauber.

## Out of Scope

- TLS-spezifische Anpassungen (folgen separat, falls nötig).
- Scale-down von Worker-Threads zur Laufzeit (per Design ausgeschlossen, §9).
