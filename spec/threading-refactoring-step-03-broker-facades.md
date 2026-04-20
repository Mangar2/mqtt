# Step 03 — Broker Facade Extraction (Phase A)

Bezug: `threading-refactoring.md` §5.3, §6, §12 Phase A

## Ziel

Broker-Klasse schrumpft drastisch. Verantwortlichkeiten wandern in dedizierte Facade- und Registry-Klassen, jede mit eigenem internen Lock. **Threading-Modell bleibt unverändert.** Nach diesem Schritt: Broker compileable, lauffähig, alle bestehenden Tests grün.

## Warum als drittes

- Unabhängig von Step 01 / 02 — kann technisch parallel laufen, wird aber sequenziell ausgeführt für sauberen Review.
- Reduziert Komplexität des Broker, bevor in Step 05 die Threading-Integration erfolgt.
- Erfüllt Design-Regel §2.1 (Broker schrumpft) und §2.2 (keine Locks im Broker).

## Zu erstellende Dateien (alle in `src/broker/`)

| Datei | Zeilen Ziel | Extrahiert aus Broker |
|-------|-------------|----------------------|
| `enhanced_auth_registry.h/.cpp` | ~50h / ~60cpp | `pending_enhanced_auth_` + `active_enhanced_auth_` Maps + Lookup/Insert/Erase. Eigener `std::mutex`. |
| `connect_facade.h/.cpp` | ~60h / ~180cpp | `handle_connect`, `handle_auth_packet`, `handle_reauthenticate`, `complete_connect_success`, `map_auth_error_to_reason`, `protocol_error_result`, `emit_connect_trace`. Hält Referenzen auf SessionManager, WillPublisher, EnhancedAuthRegistry, StructuredTracer. Eigener `std::mutex`. |
| `disconnect_facade.h/.cpp` | ~40h / ~100cpp | `handle_disconnect`, `handle_connection_lost`, `is_disconnect_expiry_override_valid`. Eigener `std::mutex`. |
| `publish_facade.h/.cpp` | ~40h / ~120cpp | `handle_publish`. Eigener `std::mutex`. |
| `subscribe_facade.h/.cpp` | ~40h / ~100cpp | `handle_subscribe`, `handle_unsubscribe`. Eigener `std::mutex`. |
| `tick_handler.h/.cpp` | ~30h / ~60cpp | `tick`, `apply_trace_system_message`. Eigener `std::mutex`. |
| `broker_module_factory.h/.cpp` | ~30h / ~180cpp | `create_modules()`. Kein Lock — nur beim Startup aufgerufen. |
| `persistence_coordinator.h/.cpp` | ~30h / ~80cpp | `load_persistence`, `flush_persistence`. Kein Lock — nur Startup/Shutdown. |
| `SPEC.md` | — | Update: neue Klassen, Lock-Map. |

## Anpassungen am Broker

- `broker_mutex_` entfernen.
- `register_connection_locked` / `unregister_connection_locked` entfernen — Logik liegt schon in `active_connection_registry`; Trace-Aufrufe wandern in passende Facade.
- Broker-Member: jeweils ein `unique_ptr` (oder Wert) pro Facade/Registry/Coordinator.
- Broker-Methoden delegieren ohne Lock auf die Facade-Methoden.

## Unit-Tests

Für jede neue Klasse co-located neben der .cpp:

### `enhanced_auth_registry.test.cpp`
- `insert_pending_then_lookup_returns_value`.
- `promote_to_active_removes_from_pending`.
- `erase_active_clears_entry`.
- `concurrent_insert_lookup_safe` — Sanitizer-Lauf.

### `connect_facade.test.cpp`
- `handle_connect_returns_success_for_valid_clean_start`.
- `handle_connect_rejects_protocol_error_with_correct_reason_code`.
- `handle_auth_packet_continues_enhanced_auth_flow`.
- `handle_reauthenticate_uses_active_auth_state`.
- `emit_connect_trace_writes_expected_fields`.

### `disconnect_facade.test.cpp`
- `handle_disconnect_with_normal_reason_does_not_publish_will`.
- `handle_disconnect_with_will_message_publishes_will`.
- `handle_connection_lost_triggers_will_and_unregister`.
- `expiry_override_validation_accepts_in_range_and_rejects_out_of_range`.

### `publish_facade.test.cpp`
- `handle_publish_qos0_routes_without_response`.
- `handle_publish_qos1_emits_puback`.
- `handle_publish_qos2_emits_pubrec`.
- `handle_publish_invalid_topic_returns_protocol_error`.
- `concurrent_publish_from_many_clients_routes_each_message_once`.

### `subscribe_facade.test.cpp`
- `handle_subscribe_single_topic_returns_granted_qos`.
- `handle_subscribe_invalid_filter_returns_failure_reason_code`.
- `handle_unsubscribe_removes_existing_subscription`.
- `handle_unsubscribe_unknown_filter_returns_no_subscription_existed`.

### `tick_handler.test.cpp`
- `tick_advances_will_timer_and_publishes_when_expired`.
- `tick_expires_session_after_session_expiry_interval`.
- `apply_trace_system_message_publishes_to_sys_topic`.

### `broker_module_factory.test.cpp`
- `create_modules_returns_all_required_modules`.
- `create_modules_wires_dependencies_consistently`.

### `persistence_coordinator.test.cpp`
- `load_persistence_restores_retained_messages_and_sessions`.
- `flush_persistence_writes_pending_state_to_store`.

### Broker-Test (Update)
- `broker_holds_no_mutex_field` — Reflection / grep-Test im Build (oder einfach Code-Review-Kriterium).
- Bestehender Broker-Test angepasst, falls private Methoden direkt getestet wurden — jetzt über Facade-Tests.

## Akzeptanzkriterien

1. **Broker.h < 350 Zeilen, Broker.cpp < 165 Zeilen** (§6 / §15.3).
2. Kein `mutex_` / `shared_mutex_` Member in `broker.h`.
3. Alle bestehenden Unit- und Integrationstests **unverändert grün** (inkl. 18.x Load-Tests bis aktuelle Stage).
4. Alle neuen Facade-Tests grün via `python test/run_coverage.py`.
5. ConnectionManager unverändert.
6. Threading-Modell unverändert (per-connection Threads bleiben).
7. Zeilenlimits pro Facade laut Tabelle eingehalten.

## Out of Scope

- Kein IoReactor, kein WorkerPool, kein ConnectionManager-Umbau.
- Keine Performance-Änderungen erwartet — pure Code-Restrukturierung.
