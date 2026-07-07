# YAHA INI Config Fallback Warning Unification Plan

## Goal

Move config-fallback-warning handling into `IniDocument` itself so every YAHA client stops
duplicating a local `logConfigFallbackWarning` function and the manual
"fetch raw value + build message + log" block after every `readUnsigned`/`readBool` call.

## Decision Record (from design discussion)

- `IniDocument` gets a `serviceName` (set once when the document is created) and a list of
  warning-handler callbacks (`add`, `clear`). A default handler (current `std::cerr` format)
  is installed automatically, so clients do not need to configure anything to keep today's
  behavior.
- Callback signature: `(serviceName, sectionName, keyName, rawValue, defaultValue, reasonText)`.
  `sectionName`/`keyName`/`rawValue`/`reasonText` are known/derived by `IniDocument` itself.
  Only `defaultValue` must be supplied by the caller.
- `readUnsigned`/`readBool` keep the explicit two-step call pattern at call sites
  (`const auto x = document.readX(...); if (x.has_value()) { output.field = *x; }`).
  This was a deliberate choice: an all-in-one "read and assign with fallback" helper was
  rejected as hidden logic — the explicit `if` stays readable at every call site.
- Return type simplifies from `pair<optional<T>, string>` to plain `optional<T>`. The reason
  string is no longer returned to the caller — it is produced and consumed inside
  `IniDocument` and handed to the warning handlers directly.
- Missing key stays silent (no handler call, same as today). Only an out-of-range/unparsable
  *present* value triggers a warning.
- For composite fallback cases that do not go through `readUnsigned`/`readBool` directly
  (message-log sub-config, mqtt sub-config failures where the reason text comes from another
  module), keep a public `IniDocument::reportFallback(sectionName, keyName, rawValue,
  defaultValue, reasonText)` that fires the same handler list explicitly.

## Scope

### `src/yaha/ini` (core change)

- `ini_document.h` / `ini_document.cpp`
- `test/ini_document_test.cpp`, `test/ini_document_typed_read_test.cpp`
- `SPEC.md`

### Files with a local `logConfigFallbackWarning` duplicate to remove

- `yaha/automation_client/automation_client_app.cpp`
- `yaha/broker_connector_client/broker_connector_client_app.cpp`
- `yaha/file_store_client/file_store_client_app.cpp`
- `yaha/http_mqtt_interface_client/http_mqtt_interface_client_app.cpp`
- `yaha/http_mqtt_interface_client/internal/http_mqtt_interface_client_app_helpers.cpp`
- `yaha/http_mqtt_interface_client/internal/http_mqtt_interface_client_app_internal.h`
- `yaha/message/message_log_service.cpp`
- `yaha/message_store_client/message_store_client_app.cpp`
- `yaha/mqtt_client/mqtt_client_config.cpp`
- `yaha/opensensemap_client/opensensemap_client_app.cpp`
- `yaha/pushover_client/pushover_client_app.cpp`
- `yaha/remote_service_client/remote_service_client_app.cpp`
- `yaha/rs485_interface_client/rs485_interface_client_app.cpp`
- `yaha/serial_device_client/serial_device_client_config.cpp`
- `yaha/value_service_client/value_service_client_app.cpp`
- `yaha/zwave_client/zwave_client_app.cpp`

### `IniDocument::loadFromFile` call sites needing the new `serviceName` argument

- 12 `*_main.cpp` files under `src/` (one per client executable)
- ~50 test call sites across `src/yaha/**/test/*.cpp` (mechanical update)

### Not in scope

- Changing INI file syntax or parsing rules.
- Changing default values themselves for any config key.
- Any behavior change to the default warning text format (must match today's output).

## Target API (draft)

```cpp
using ConfigWarningHandler = std::function<void(
    std::string_view serviceName,
    std::string_view sectionName,
    std::string_view keyName,
    const std::string& rawValue,
    const std::string& defaultValue,
    const std::string& reasonText)>;

class IniDocument {
public:
    [[nodiscard]] static IniDocument loadFromFile(
        const std::filesystem::path& filePath,
        std::string serviceName);

    void addWarningHandler(ConfigWarningHandler handler);
    void clearWarningHandlers();

    void reportFallback(
        std::string_view sectionName,
        std::string_view keyName,
        const std::string& rawValue,
        const std::string& defaultValue,
        const std::string& reasonText) const;

    [[nodiscard]] std::optional<std::uint64_t> readUnsigned(
        std::string_view sectionName,
        std::string_view key,
        std::uint64_t minValue,
        std::uint64_t maxValue,
        const std::string& defaultValueText) const;

    [[nodiscard]] std::optional<bool> readBool(
        std::string_view sectionName,
        std::string_view key,
        bool defaultValue) const;

private:
    std::string serviceName_{};
    std::vector<ConfigWarningHandler> warningHandlers_{};
};
```

Call-site shape after migration (unchanged readability, less boilerplate):

```cpp
const auto port = document.readUnsigned(
    "filestore", "port", 1U, 65535U, std::to_string(output.fileStorePort));
if (port.has_value()) {
    output.fileStorePort = static_cast<std::uint16_t>(*port);
}
```

## Migration Phases

Phase boundaries are chosen so the build stays green (all tests pass) after every phase,
per the project's mandatory build/test gate. `readUnsigned`/`readBool` change their return
shape (`pair<optional<T>, string>` -> `optional<T>`), which breaks every call site at once —
that change must land together with consumer migration (Phase 3), not standalone.

### Phase 1: Extend `IniDocument` (additive only, nothing breaks)

- Add `serviceName_` member. `loadFromFile(path, serviceName = {})` gets a defaulted
  parameter so every existing call site (12 `*_main.cpp`, ~50 test files) keeps compiling
  unchanged.
- Add `ConfigWarningHandler`, `warningHandlers_`, `addWarningHandler()`, `clearWarningHandlers()`.
- Install one default handler in `loadFromFile` reproducing today's `std::cerr` format
  exactly (same field order/labels as current `logConfigFallbackWarning`).
- Add `reportFallback()` as the shared primitive (fires all registered handlers with
  `serviceName_` plus the given section/key/rawValue/defaultValue/reasonText). Nothing calls
  it yet in production code — it is exercised directly by new unit tests and will be wired
  into `readUnsigned`/`readBool` and used by composite-fallback call sites in Phase 3.
- `readUnsigned`/`readBool` are NOT touched in this phase (signature/behavior unchanged).
- New test file `test/ini_document_warning_handler_test.cpp` covers: default handler text
  format, multiple handlers all invoked, `clearWarningHandlers` removes the default handler
  too, `reportFallback` forwards fields and `serviceName` correctly.
- Update `src/yaha/ini/SPEC.md` API table.

Status: implemented.

### Phase 2: Pass real `serviceName` at production `loadFromFile` call sites

- Add the `serviceName` argument at all 12 `*_main.cpp` call sites, reusing the exact service
  name string each client already uses today in its local `logConfigFallbackWarning` calls.
- Test call sites keep relying on the default parameter unless a test specifically exercises
  warning-handler/service-name behavior (no mass edit of ~50 test files required).

Status: implemented.

### Phase 3: Change `readUnsigned`/`readBool` signature and migrate client config files

Must happen together (signature change + all consumers) to keep the build compiling. For
each of the 16 files listed above:

- Add `defaultValue` argument to `readUnsigned`/`readBool`, drop the `string` half of the
  return pair, fire warning handlers internally (via the Phase-1 `reportFallback` primitive)
  on invalid (not missing) values.
- Remove the local `logConfigFallbackWarning` function (definition + forward declaration).
- Replace `readUnsigned`/`readBool` call sites: add `defaultValue` argument, drop the manual
  `if (!result.second.empty()) { rawValue fetch; logConfigFallbackWarning(...); }` block,
  keep the explicit `if (result.has_value()) { output.field = ...; }` assignment.
- Replace composite-fallback call sites (message-log sub-config, mqtt sub-config failures)
  with `document.reportFallback(...)`.

Notable behavior deltas found during migration (both are consistency fixes, not regressions):
- `message_log_service.cpp` and `mqtt_client_config.cpp` previously hardcoded the wrong
  service name in their local warning function (`"message_log_service"` / `"mqtt"`
  regardless of which client actually loaded the config). Now the warning carries the
  real owning client's `serviceName` (e.g. `"automation_client"`), since it comes from the
  shared `document.reportFallback`.
- `remote_service_client_app.cpp`'s `filestore.port` handling previously fired two warnings
  for one invalid value (the generic one plus a second "missing required setting" one from
  an always-taken `else` branch). Now only one warning fires per case: the auto-fired one
  for invalid values, or an explicit `reportFallback` only when the key is truly absent.

Status: implemented.

### Phase 4: Test and verification pass

- Update client config/app tests that assert on stderr warning text, if any (grep for
  `config_fallback` / `logConfigFallbackWarning` expectations in tests first — none found).
- Run `python3 test/run_coverage_clients.py`: 1152/1152 tests OK, threshold MET (all touched
  production files, including `ini_document.cpp`, stay >= 80% regions/functions/lines/branches).

Status: implemented.

### Phase 5: Cleanup and guardrails

- Grep confirms no remaining local `logConfigFallbackWarning` definitions anywhere (only
  `src/yaha/ini/ini_document.cpp`'s default handler and its own `SPEC.md` mention it).
- Grep confirms no remaining call site uses the old `pair<optional<T>, string>` return shape
  (`.first`/`.second` on a `readUnsigned`/`readBool` result).
- `src/yaha/ini/SPEC.md` reflects final API.

Status: implemented.

## Acceptance Criteria

- Only one place in the codebase formats/logs config-fallback warnings: `IniDocument`'s
  default handler (plus whatever handlers callers explicitly add).
- No client file declares or defines its own `logConfigFallbackWarning`.
- Default warning output text is unchanged from today (no operational log-format regression).
- Missing-key behavior (silent, default kept) is unchanged.
- All call sites keep an explicit `if (result.has_value())` assignment step — no hidden
  read-and-assign-with-fallback helper is introduced.
- Full test suite green after migration.

## Risks and Mitigations

- Risk: default handler text drifts from current format, breaking anything that scrapes
  stderr logs.
  - Mitigation: byte-for-byte comparison test against today's `logConfigFallbackWarning`
    output before deleting the old per-file functions.
- Risk: large mechanical diff across ~50 test files for the new `serviceName` argument hides
  an unrelated behavior change.
  - Mitigation: do Phase 2 as its own commit, no logic changes bundled in.
- Risk: composite-fallback call sites (message-log/mqtt sub-config) get missed and keep
  calling a now-deleted local function.
  - Mitigation: Phase 3 checklist explicitly covers both `readUnsigned`/`readBool` sites and
    `reportFallback` sites per file, verified by `grep -rn logConfigFallbackWarning src/`
    returning empty at the end of Phase 3.
