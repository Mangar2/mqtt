# TODO Plan: Robust INI Loading Across YAHA Services

## Scope

This plan defines a uniform hardening strategy for all YAHA services that load settings from INI files.

Primary goals:
- Inventory all affected services and config loaders.
- Remove arbitrary value caps that have no domain justification.
- Make INI parsing fault-tolerant: log to `std::cerr` and keep/use default values when one value is invalid.
- Ensure services still start with meaningful defaults even if one or more INI values are invalid.

## Services To Review

Standalone service entrypoints:
- src/yaha_automationclient_main.cpp
- src/yaha_brokerconnectorclient_main.cpp
- src/yaha_filestoreclient_main.cpp
- src/yaha_httpmqttinterfaceclient_main.cpp
- src/yaha_msgstoreclient_main.cpp
- src/yaha_remoteserviceclient_main.cpp
- src/yaha_rs485interfaceclient_main.cpp
- src/yaha_valueserviceclient_main.cpp
- src/yaha_zwaveclient_main.cpp

Shared/config loader modules:
- src/yaha/mqtt_client/mqtt_client_config.cpp
- src/yaha/message/message_log_service.cpp
- src/yaha/automation_client/automation_client_app.cpp
- src/yaha/broker_connector_client/broker_connector_client_app.cpp
- src/yaha/file_store_client/file_store_client_app.cpp
- src/yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.cpp
- src/yaha/message_store_client/message_store_client_app.cpp
- src/yaha/remote_service_client/remote_service_client_app.cpp
- src/yaha/rs485_interface_client/rs485_interface_client_app.cpp
- src/yaha/value_service_client/value_service_client_app.cpp
- src/yaha/zwave_client/zwave_client_app.cpp

## Problem Statement

Current behavior in several loaders is fail-fast for single invalid keys:
- One malformed value can abort full config loading.
- Some numeric limits appear implementation-chosen (technical caps) rather than domain-required constraints.
- Error reporting is often returned as function error text, but not consistently emitted to `std::cerr` in all branches.

This causes unnecessary startup failures and weak operator feedback.

## Target Behavior

For each config key:
- Missing key: keep existing default value.
- Valid key: override default with configured value.
- Invalid key value: emit deterministic `std::cerr` warning and keep default.

For each service loader:
- Continue loading after non-fatal key errors.
- Return success when required structural prerequisites are still satisfiable.
- Abort only on truly non-recoverable required configuration gaps.

## Non-Recoverable vs Recoverable Inputs

Recoverable (must fallback to default):
- Invalid bool/number parse for optional settings.
- Invalid optional timing/retry/toggle values.
- Invalid optional monitoring/logging values.

Potentially non-recoverable (can still fail):
- Missing mandatory identity/path values where no safe default exists and service behavior would be undefined.
- Structurally broken sections where no deterministic fallback can preserve minimal contract semantics.

## Arbitrary Limit Removal Policy

Rule:
- Keep domain constraints.
- Remove arbitrary upper bounds that were introduced without business/domain semantics.
- Use type-safe technical bounds where needed to prevent overflow.

Examples to apply:
- Replace arbitrary max caps like `60000`, `600000`, `86400`, `1000` where they are not domain rules.
- Keep strict bounds where protocol/domain requires them (e.g., QoS 0..2, port 1..65535, node id ranges).

## Logging Contract

All fallback events must write to `std::cerr` and flush.

Recommended format:
- `<service>[warn] config_fallback section=<section> key=<key> value='<raw>' default='<default>' reason='<reason>'`

Requirements:
- One warning per invalid key occurrence.
- Message must include key identity and selected default.
- No silent fallback paths.

## Implementation Work Packages

### WP1: Shared loader hardening
- Harden `tryLoadMqttClientConfigFromIni` in src/yaha/mqtt_client/mqtt_client_config.cpp.
- Harden `tryLoadMessageLogConfigFromIni` in src/yaha/message/message_log_service.cpp.
- Introduce reusable helper(s) for robust parse-with-default behavior.

### WP2: Service-specific loader hardening
- Apply fallback behavior in all listed service loader modules.
- Preserve current defaults from each config struct.
- Replace fail-fast parse branches with warning + default fallback for recoverable keys.

### WP3: Remove arbitrary caps
- Audit each `readUnsigned(... min, max)` and equivalent parse range.
- For non-domain maxima, migrate to technically safe maxima.
- Keep explicit domain ranges unchanged.

### WP4: Startup resilience validation
- Ensure main entrypoint behavior remains startup-capable when fallback happened.
- Keep hard fail only for non-recoverable required prerequisites.

### WP5: Tests and coverage
- Add/update unit tests per loader module:
  - Invalid numeric value -> warning emitted, default kept, loader succeeds.
  - Invalid bool value -> warning emitted, default kept, loader succeeds.
  - Missing optional value -> default kept, loader succeeds.
  - Non-recoverable required value missing -> deterministic failure retained.
- Run scoped coverage scripts for changed areas.

### WP6: Documentation sync
- Update affected module `SPEC.md` files with fallback behavior.
- Update README configuration docs if key semantics/default behavior changed.

## Acceptance Criteria

1. Service inventory is complete for all YAHA standalone clients and shared INI loaders.
2. Arbitrary upper bounds are removed or replaced with domain/technical rationale.
3. Invalid recoverable INI values do not abort service config load.
4. Every fallback writes deterministic warning output to `std::cerr`.
5. Services remain startup-capable with defaults after fallback.
6. Non-recoverable missing requirements still fail deterministically.
7. Unit tests cover fallback and non-recoverable cases.
8. All touched SPEC docs are updated and consistent.

## Rollout Order

1. Shared loaders (`mqtt_client_config`, `message_log_service`).
2. High-impact service loaders (`zwave`, `rs485_interface`, `message_store`, `http_mqtt_interface`).
3. Remaining service loaders (`automation`, `value_service`, `remote_service`, `file_store`, `broker_connector`).
4. Main startup path verification.
5. Final test/coverage and SPEC synchronization.

## Risk Notes

- Over-broad fallback can hide truly broken configs; therefore classify recoverable vs non-recoverable explicitly.
- Removing maxima without overflow checks can introduce runtime instability; enforce type-safe bounds.
- Warning spam risk exists with repeated parse attempts; keep warning emission tied to load time, not runtime loops.
