# Implementation Plan: SerialDevice Client 1.0

This plan defines the implementation sequence for a new YAHA SerialDevice client with strict 1:1 behavioral parity to legacy `@mangar2/serialdevice`.

Primary reconstruction source:
- [SPEC-serialdevice-reconstruction.md](../@mangar2/serialdevice/SPEC-serialdevice-reconstruction.md)

Related YAHA contracts:
- [SPEC-IMqttComponent.md](./SPEC-IMqttComponent.md)
- [SPEC-message.md](./SPEC-message.md)

## Goal

Implement one standalone YAHA client that exactly reproduces legacy SerialDevice behavior:
- serial input parsing and normalization
- serial->mqtt mapping and publish semantics
- mqtt->serial mapping and wire serialization
- keep-alive, retry/open behavior, queue spacing
- trace controls and subscription derivation

No functional simplification is allowed.

## Scope

In scope:
- New SerialDevice YAHA domain component implementing IMqttComponent
- Serial protocol adapter and mapping logic with parity behavior
- Standalone runtime config mapping and executable composition
- Full parity test harness with oracle-generated golden data
- Build and deployment wiring for new client executable

Out of scope:
- Protocol redesign or semantic cleanup
- Changes to unrelated YAHA clients/components
- Backward-incompatible behavior improvements

## Target module structure

1. Domain module: src/yaha/serial_device/
- SerialDevice config type
- SerialDevice component implementation
- subscribe derivation
- mqtt->serial and serial->mqtt mapping
- parser and serializer helpers

2. Runtime module: src/yaha/serial_device_client/
- INI runtime config parser and mapping
- runtime composition helpers (component + mqtt runtime + serial adapter)

3. Standalone entrypoint:
- src/yaha_serialdeviceclient_main.cpp

## Mandatory artifacts

1. Executable
- yahaserialdeviceclient target in CMake
- install wiring analogous to existing YAHA clients

2. Runtime config
- ini template for serialdevice client
- parser for all required keys and defaults in reconstruction spec

3. Deployment integration
- create_yaha_deployment.py component integration
- deploy_yaha_scp.py component deployment integration
- deployment output path for binary + ini (+ service if used)

4. Quality proof
- oracle datasets generated from original JS implementation
- parity tests that compare new client outputs against oracle
- strict fail-on-delta gate in CI/test scripts

## Phase plan

## Phase 0: Oracle foundation first (mandatory)

Step 0.1 Build legacy oracle harness
- Implement a small deterministic harness that runs legacy JS modules directly:
  - parseserialdata
  - mqtttoserial
  - serialtomqtt
  - serialmessagetostring
  - derivesubscribes
  - selected serialdevice runtime transitions (where deterministic)
- Feed fixed input vectors and capture exact outputs/errors/logical events.

Step 0.2 Define canonical oracle format
- Store as stable JSON files with explicit schema:
  - test id
  - input
  - expected output OR expected error text
  - notes (source, rationale)
- Include protocol-level and component-level vectors.

Step 0.3 Freeze oracle datasets
- Commit oracle files under spec or testdata path.
- Add change policy: oracle files are immutable unless legacy baseline change is explicitly approved.

Deliverable:
- reproducible golden datasets derived from original logic.

## Phase 1: Contracts and configuration mapping

Step 1.1 Define C++ config contract matching reconstruction spec
- keys, defaults, enums, and range checks
- preserve legacy quirks where specified

Step 1.2 Implement INI -> domain config mapping
- deterministic error messages
- strict key validation and fallback behavior parity

Step 1.3 Implement subscription derivation contract
- exact topic wildcard expansion and `$SYS/serialdevice/#`

Deliverable:
- validated config + deterministic subscriptions matching oracle.

## Phase 2: Data model, parser, serializer

Step 2.1 Implement internal serial message model
- fields and semantics equal to legacy SerialMessage

Step 2.2 Implement stream parser parity
- noise skip behavior
- bracket-based extraction
- object/array format transforms
- fs20 `/set` action behavior

Step 2.3 Implement wire serialization parity
- switch/i2c/serial/fs20 output formats
- exact switch bit semantics (`SWITCH_ON`, `SWITCH_OFF`)

Deliverable:
- parser+serializer outputs identical to oracle vectors.

## Phase 3: Mapping parity (core domain)

Step 3.1 Implement MQTT -> serial mapper
- topicMap direct path
- command suffix route
- receiver prefix route
- valueMap and fallback coercions
- preserve known quirk in range-check behavior as documented

Step 3.2 Implement serial -> MQTT mapper
- topic begin/end derivation
- commandMap + sendMap fallback
- value reverse-mapping
- switch expansion rules

Step 3.3 Implement explicit error-path parity
- unknown address/command/topic/value behavior
- ensure throws/logs match expected categories and messages where oracle defines them

Deliverable:
- conversion parity at function level proven against oracle.

## Phase 4: Service runtime parity

Step 4.1 Implement component runtime lifecycle
- run/open/close semantics
- serial receive callback processing
- keep-alive loop with exact payload `at`

Step 4.2 Implement send queue and retry behavior
- task queue spacing 100ms
- send retry loop + reopen strategy
- open retry loop parameters and delay behavior

Step 4.3 Implement publish path semantics
- set-suffix normalization/re-add logic
- reply matching interaction semantics
- qos assignment and callback invocation

Step 4.4 Implement trace controls
- `$SYS/serialdevice/trace/set` handling
- messages/internal log behavior parity where observable

Deliverable:
- runtime behavior parity in deterministic integration tests.

## Phase 5: YAHA standalone composition

Step 5.1 Implement serialdevice client runtime composition
- component instantiation from config
- mqtt runtime wiring
- serial adapter wiring

Step 5.2 Add standalone main
- thin entry point
- parse config, start runtime, clean shutdown path

Step 5.3 Add build integration
- target, include paths, source ownership in CMake

Deliverable:
- runnable standalone client with clean boundaries.

## Phase 6: Deployment integration

Step 6.1 Package generation integration
- add serialdevice component to create_yaha_deployment.py

Step 6.2 SCP deployment integration
- add component support in deploy_yaha_scp.py
- preserve protected-config/no-overwrite ini behavior

Step 6.3 Deploy verification
- verify deployment artifacts and remote install path

Deliverable:
- serialdevice client deployable with existing YAHA workflow.

## Oracle strategy and where oracle is mandatory

Oracle is mandatory at every interface boundary where parity is required.

1. Oracle A: parser behavior
- Input: byte chunks with noise/partial frames/multi-frame payloads
- Output: sequence of normalized serial messages or null
- Must include malformed JSON cases and edge boundaries

2. Oracle B: mqtt->serial mapping
- Input: topic/value/config
- Output: serial message fields and errors
- Must include ambiguous command suffix and valueMap edge cases

3. Oracle C: serial->mqtt mapping
- Input: serial message/config
- Output: mqtt message array (topic/value/reason/action effects) and errors
- Must include switch multi-publish behavior and fs20 sendMap fallback

4. Oracle D: serial wire serialization
- Input: serial message
- Output: exact wire string
- Must include switch bit calculations and all interface types

5. Oracle E: subscription derivation
- Input: options.interfaces + qos
- Output: full subscription object
- Must include system topic and empty receiverMap behavior

6. Oracle F: runtime behavior slices
- Input: deterministic event scripts (mqtt in, serial in, timer ticks)
- Output: ordered publish events and serial send payloads
- Must include keep-alive and retry/open scenarios with deterministic adapters/mocks

## Oracle data generation process

Step O1. Create deterministic generator script
- Runs legacy JS logic in a controlled environment.
- Emits sorted, stable JSON.

Step O2. Generate datasets per oracle family A-F
- One file per family and scenario group.

Step O3. Validate oracle consistency
- Re-run generator twice and assert byte-identical output.

Step O4. Lock oracle baseline
- Commit generated files and generator version metadata.

Step O5. Use oracle in C++ tests
- Load JSON vectors and run parameterized tests.
- Compare exact structural and textual outputs.

## Oracle file layout (normative)

Store all oracle assets under one stable root:

- `test/oracle/serialdevice/README.md`
- `test/oracle/serialdevice/schema/oracle-case.schema.json`
- `test/oracle/serialdevice/schema/oracle-suite.schema.json`
- `test/oracle/serialdevice/generator/generate_oracle_serialdevice.js`
- `test/oracle/serialdevice/generator/config/default-oracle-config.json`
- `test/oracle/serialdevice/data/A-parser/core.json`
- `test/oracle/serialdevice/data/A-parser/malformed.json`
- `test/oracle/serialdevice/data/B-mqtt-to-serial/core.json`
- `test/oracle/serialdevice/data/B-mqtt-to-serial/errors.json`
- `test/oracle/serialdevice/data/C-serial-to-mqtt/core.json`
- `test/oracle/serialdevice/data/C-serial-to-mqtt/switch.json`
- `test/oracle/serialdevice/data/D-wire-serialization/core.json`
- `test/oracle/serialdevice/data/E-subscribes/core.json`
- `test/oracle/serialdevice/data/F-runtime-slices/keepalive.json`
- `test/oracle/serialdevice/data/F-runtime-slices/retry-open.json`
- `test/oracle/serialdevice/data/manifest.json`

Rules:
- `data/**` files are generated, committed, and treated as baseline artifacts.
- `schema/**` files are hand-maintained and versioned.
- `manifest.json` is generated and contains checksums for every oracle data file.

## Oracle JSON schema (normative)

Two levels are required.

1. Suite file schema (`oracle-suite.schema.json`)
- `suiteId: string` (example: `B-mqtt-to-serial-core`)
- `oracleVersion: string` (semantic version)
- `legacySource: object`
  - `module: string`
  - `commit: string`
  - `generatorVersion: string`
- `createdAtUtc: string` (ISO-8601)
- `deterministicSeed: string`
- `cases: array<OracleCase>`

2. Case schema (`oracle-case.schema.json`)
- `id: string` (unique in suite)
- `title: string`
- `tags: string[]`
- `configRef: string` (name of config fixture)
- `input: object` (family-specific)
- `expected: object | array | string | number | null`
- `expectedError: string | null`
- `expectedOrder: string[] | null`
- `notes: string`

Validation rules:
- Exactly one of `expected` or `expectedError` must be present as authoritative output.
- If event order matters, `expectedOrder` must be provided and compared exactly.
- Numeric values must be encoded as JSON numbers, not strings, unless legacy output is string.

## Oracle generator CLI contract (normative)

Canonical command:

`node test/oracle/serialdevice/generator/generate_oracle_serialdevice.js --config test/oracle/serialdevice/generator/config/default-oracle-config.json --out test/oracle/serialdevice/data --families A,B,C,D,E,F`

Supported flags:
- `--config <path>`: generator config path (required)
- `--out <path>`: output root for generated oracle files (required)
- `--families <csv>`: subset of `A,B,C,D,E,F` (optional, default all)
- `--verify-determinism`: run generation twice and assert byte-identical output
- `--update-manifest`: recompute and write `manifest.json`
- `--fail-on-diff`: exit non-zero if tracked oracle files would change

Exit codes:
- `0`: success, outputs valid
- `2`: schema validation failed
- `3`: deterministic replay failed
- `4`: output diff detected with `--fail-on-diff`
- `10`: internal generator/runtime failure

Generator invariants:
- Stable sorting for all object keys in output.
- Stable case ordering by `id`.
- UTF-8, LF line endings.

## Oracle to C++ test-loader mapping (normative)

Each oracle family maps to dedicated C++ parameterized tests.

1. Oracle A -> parser tests
- target file: `src/yaha/serial_device/test/serial_parser_oracle_test.cpp`
- loads: `test/oracle/serialdevice/data/A-parser/*.json`
- assertion: exact parsed message sequence and null-return behavior

2. Oracle B -> mqtt->serial mapper tests
- target file: `src/yaha/serial_device/test/mqtt_to_serial_oracle_test.cpp`
- loads: `test/oracle/serialdevice/data/B-mqtt-to-serial/*.json`
- assertion: field-by-field serial message equality and exact error text where defined

3. Oracle C -> serial->mqtt mapper tests
- target file: `src/yaha/serial_device/test/serial_to_mqtt_oracle_test.cpp`
- loads: `test/oracle/serialdevice/data/C-serial-to-mqtt/*.json`
- assertion: exact message array cardinality, topic/value/reason/action, and order

4. Oracle D -> wire serialization tests
- target file: `src/yaha/serial_device/test/serial_wire_oracle_test.cpp`
- loads: `test/oracle/serialdevice/data/D-wire-serialization/*.json`
- assertion: exact serialized payload bytes/string

5. Oracle E -> subscribe derivation tests
- target file: `src/yaha/serial_device/test/subscribes_oracle_test.cpp`
- loads: `test/oracle/serialdevice/data/E-subscribes/*.json`
- assertion: exact subscribe map equality

6. Oracle F -> runtime slice tests
- target file: `src/yaha/serial_device/test/serial_device_runtime_oracle_test.cpp`
- loads: `test/oracle/serialdevice/data/F-runtime-slices/*.json`
- assertion: exact ordered outputs (publish events, serial sends, retries, keepalive)

Shared test utility:
- `src/yaha/serial_device/test/oracle_loader.h`
- `src/yaha/serial_device/test/oracle_loader.cpp`

Loader responsibilities:
- validate each suite file against schema before use
- normalize path-independent metadata fields
- provide typed test vectors to Catch2 generators

## Oracle lifecycle and update policy (normative)

1. New behavior claim
- Add or modify generator input vectors first.
- Regenerate oracle data.
- Review diff and document rationale in changelog/spec.

2. Refactor-only claim
- Run generator with `--fail-on-diff --verify-determinism`.
- Any oracle data change is a blocker.

3. CI gate
- CI must run:
  - oracle schema validation
  - determinism check
  - all oracle-driven tests
- Any failure blocks merge.

4. Review requirement
- Any change in `test/oracle/serialdevice/data/**` requires explicit reviewer note:
  - why legacy baseline changed or why vectors were incomplete
  - what new edge case is now covered

## Oracle gate rules (blocking)

1. Any mismatch to oracle fails test run.
2. No fuzzy matching for topics, values, order, or error text where defined.
3. New behavior requires explicit oracle extension plus approval note.
4. Refactor-only changes must keep oracle outputs byte-identical.
5. CI/local coverage scripts must include oracle test suites in relevant scope.

## Test implementation plan

1. Unit tests
- parser, mapper, serializer, subscribe derivation
- fully oracle-driven parameterized tests

2. Component tests
- handleMessage, run/close behavior with deterministic mocks
- publish callback sequence assertions

3. Integration tests (targeted only)
- startup + message flow with mocked serial transport
- reconnect/retry deterministic scenarios

4. Deployment smoke tests
- package contains expected serialdevice artifacts
- deployment script handles component end-to-end

## Acceptance criteria

1. New client matches all oracle datasets A-F with zero deltas.
2. No simplification vs legacy behavior is introduced.
3. Standalone executable builds and runs from ini config.
4. Subscribe list, topic/value mapping, and wire payloads match baseline.
5. Runtime lifecycle, keep-alive, and retry behavior are parity-verified.
6. Deployment packaging and remote deploy are integrated and verified.

## Risks and mitigations

1. Hidden nondeterminism in runtime tests
- Mitigation: deterministic fake clock and mock serial adapter for oracle F.

2. Legacy quirks accidentally normalized
- Mitigation: enforce oracle gates before and after each refactor step.

3. Mapping ambiguity due to iteration order
- Mitigation: preserve deterministic map iteration strategy and validate with dedicated oracle vectors.

4. Drift between spec and implementation
- Mitigation: update reconstruction spec and this implementation plan in the same change whenever behavior understanding changes.

## Execution order summary

Phase 0 -> Phase 1 -> Phase 2 -> Phase 3 -> Phase 4 -> Phase 5 -> Phase 6

Do not start coding Phase 2+ before oracle foundation (Phase 0) is committed and green.
