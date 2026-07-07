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

### Phase 1: Extend `IniDocument`

- Add `serviceName_` member, threaded through `loadFromFile(path, serviceName)`.
- Add `ConfigWarningHandler`, `warningHandlers_`, `addWarningHandler()`, `clearWarningHandlers()`.
- Install one default handler in `loadFromFile` reproducing today's `std::cerr` format
  exactly (same field order/labels as current `logConfigFallbackWarning`).
- Add `reportFallback()` and route it through the same internal helper that
  `readUnsigned`/`readBool` use for invalid values.
- Change `readUnsigned`/`readBool` signatures: add `defaultValue` parameter, drop the
  `string` half of the return pair, fire warning handlers internally on invalid (not missing)
  values.
- Update `test/ini_document_test.cpp` and `test/ini_document_typed_read_test.cpp`:
  - default handler produces same text as today
  - `addWarningHandler`/`clearWarningHandlers` behavior
  - missing key stays silent
  - invalid value triggers handler exactly once with correct fields
- Update `src/yaha/ini/SPEC.md` API table.

Status: not started.

### Phase 2: Update `loadFromFile` call sites

- Add the `serviceName` argument at all 12 `*_main.cpp` call sites, reusing the exact service
  name string each client already uses today in its local `logConfigFallbackWarning` calls.
- Update ~50 test call sites (`src/yaha/**/test/*.cpp`) to pass a `serviceName`.

Status: not started.

### Phase 3: Migrate client config-loading files

For each of the 16 files listed above:

- Remove the local `logConfigFallbackWarning` function (definition + forward declaration).
- Replace `readUnsigned`/`readBool` call sites: add `defaultValue` argument, drop the manual
  `if (!result.second.empty()) { rawValue fetch; logConfigFallbackWarning(...); }` block,
  keep the explicit `if (result.has_value()) { output.field = ...; }` assignment.
- Replace composite-fallback call sites (message-log sub-config, mqtt sub-config failures)
  with `document.reportFallback(...)`.

Status: not started.

### Phase 4: Test and verification pass

- Update client config/app tests that assert on stderr warning text, if any (grep for
  `config_fallback` / `logConfigFallbackWarning` expectations in tests first).
- Run `/unit-test` and `/integration-test` per project skills.
- Run `/build` for affected scopes.

Status: not started.

### Phase 5: Cleanup and guardrails

- Grep confirms no remaining local `logConfigFallbackWarning` definitions anywhere.
- Grep confirms no remaining call site uses the old `pair<optional<T>, string>` return shape.
- `src/yaha/ini/SPEC.md` reflects final API.

Status: not started.

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
