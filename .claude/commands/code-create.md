Full workflow for creating or extending code in this project. Follow every step in order.

## Branch and commit workflow

**Before making any code changes, create a dedicated Git branch:**

```sh
git checkout -b <feature-or-fix-name>
```

Branch names must be short, lowercase, hyphen-separated, and descriptive of the change (e.g. `add-session-expiry`, `fix-publish-qos2`).

All code changes are committed on this branch — never directly on `master`.

**Automatic commit after successful completion:**

Once the completion checklist (see below) is fully satisfied — all tests pass **and** test coverage is ≥ 90 % for all changed modules — commit the changes automatically:

```sh
git add -A
git commit -m "<short imperative description of the change>"
```

Do **not** commit if any test fails or if coverage is below 90 %. Fix the issues first, then commit.

## Build structure — read CMakeLists.txt first

**Before writing any `.cpp` file in a new module, read `CMakeLists.txt`** to verify
the glob patterns and link structure are compatible. Never guess.

Current build structure (verified in CMakeLists.txt):

| Glob | Target | Notes |
|------|--------|-------|
| `src/*.cpp` excluding `/test/` and `main.cpp` | `mqtt-broker` **and** `mqtt-broker-tests` | `MQTT_LIB_SOURCES` feeds both |
| `src/*_test.cpp` (any depth) | `mqtt-broker-tests` only | |
| `src/main.cpp` | `mqtt-broker` only | excluded from tests via `list(FILTER … main.cpp)` |

Both targets share `src/` on their include path, so headers are included as
`codec/…`, `data_model/…`, etc.

Other facts (do not read CMakePresets.json):
- Test framework: **Catch2 v3** (`Catch2::Catch2WithMain`), already linked.
- Compile flags: `-Wall -Wextra -Wpedantic -Werror` on both targets.
- CMake re-globs at every build invocation — no manual file registration needed.

## File count rule

Keep the number of code files per directory (and subdirectory) below 10.
Before adding a new file, check the current count. If a directory is near the
limit, split into subdirectories first.

## Documentation-first: target spec (SPEC.md)

Before writing any code, create or update a `SPEC.md` in the target directory.

- One `SPEC.md` per directory.
- A `SPEC.md` in a parent directory summarises all its subdirectories.
- `SPEC.md` describes: purpose, public API, data structures, behaviour, constraints.
- The code must match `SPEC.md` — it is the implementation guide.

## Documentation and code stay in sync

- When new insights contradict `SPEC.md`: update `SPEC.md` first, then continue.
- Before a new feature: update `SPEC.md` first, then implement.
- Never let code and documentation diverge.

## No redundant regeneration

Only generate or rewrite code when behaviour actually changes.
Never regenerate a file with identical behaviour. If a file is correct, leave it.

## Unit-test specification (TEST_SPEC.md)

For every directory with testable code, maintain a `TEST_SPEC.md`.

- `TEST_SPEC.md` lists every planned unit test: name, scenario, input, expected.
- Before writing new tests: add to `TEST_SPEC.md` first, then implement.
- Keep `TEST_SPEC.md` current — remove deleted tests, add entries for new behaviour.

## Unit-test implementation

Generate unit tests based on `TEST_SPEC.md`.

- Place test files in a `test/` subdirectory inside the code directory under test.
- The `TEST_SPEC.md` lives in that same `test/` directory.
- Follow the `/unit-test` skill for test style, coverage, and execution.
- Never implement a test not listed in `TEST_SPEC.md` — add it to the spec first.

## Build and verify

Every implementation ends with a successful build **and** all tests passing.
All commands run from the **project root** (`c:\Development\mqtt`).
Never pipe build output through `tail` or any filter — full output is required to
catch all errors and warnings.

### Standard build + test (most common)

```sh
cmake --build --preset debug && ctest --preset debug
```

### First build or after a clean

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

CMake reconfigures automatically when files are added or removed (glob
`CONFIGURE_DEPENDS`), so the two-step form is only needed the very first time or
after an explicit clean.

## Measure test coverage

Run the coverage analysis for every new or changed module.
All four commands are run sequentially — never skip or reorder them.

```sh
# 1. Configure and build with coverage instrumentation
cmake --preset test-coverage
cmake --build --preset test-coverage

# 2. Run the binary directly (NEVER ctest — it overwrites profraw per test)
LLVM_PROFILE_FILE="build/test-coverage/coverage.profraw" \
  ./build/test-coverage/mqtt-broker-tests.exe

# 3. Merge
llvm-profdata merge -sparse build/test-coverage/coverage.profraw \
  -o build/test-coverage/coverage.profdata

# 4. Report scoped to the changed module(s)
llvm-cov report build/test-coverage/mqtt-broker-tests.exe \
  -instr-profile=build/test-coverage/coverage.profdata \
  src/<module-path>/
```

After reviewing the report, if coverage is below threshold run
`llvm-cov show` on the specific file to see which lines are missed — **do not
guess from the aggregate numbers alone**.

## Completion checklist — mandatory gate

Do not mark any module complete until **every item** below passes.

| # | Criterion | How to verify |
|---|-----------|---------------|
| 1 | **Build clean** | `cmake --build --preset debug` exits 0 errors, 0 warnings |
| 2 | **No compiler warnings** | Guaranteed by `-Werror` — any warning is a build failure |
| 3 | **No linter / IDE warnings** | All clang-tidy diagnostics resolved |
| 4 | **VS Code Problems panel clear** | Use `get_errors` tool on all changed files — zero errors and zero warnings; every diagnostic reported by the IDE must be fixed before marking the task done |
| 5 | **All tests pass** | `ctest --preset debug` → `100% tests passed`; new tests appear by name |
| 6 | **Test coverage ≥ 90 %** | Regions, Functions, Lines ≥ 90 % for production files |
| 7 | **SPEC.md is current** | Every touched directory has an accurate SPEC.md |
| 8 | **TEST_SPEC.md is current** | Every test in code has a matching entry; removed tests removed from spec |
| 9 | **Doxygen on all public API** | Every header follows the documentation rules in `/cpp-dev` |

## Completion report — required format

End every implementation with this report. All sections are mandatory.

```
## Completion report — <Module name>

### Build
cmake --build --preset debug → clean (0 errors, 0 warnings)

### Tests
ctest --preset debug → 100 % passed  (<N> new tests, <M> total)

### Coverage — <module path>
<paste llvm-cov report table here>

Production headers: Regions <X>%, Functions <X>%, Lines <X>%
→ threshold met ✓  /  NOT met ✗ (list files below threshold)

### Docs
SPEC.md current: yes/no
TEST_SPEC.md current: yes/no
Doxygen on all public API: yes/no
```
