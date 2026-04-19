Full workflow for creating or extending code in this project. Follow every step in order.

## Branch and commit workflow

For all Pull Request operations, use `/github-pr` and execute its steps exactly.

**One branch per top-level module (e.g. `3. Topic Engine`).** All sub-modules (3.1, 3.2, …) are committed on the same branch. When the entire top-level module is complete, open a Pull Request and merge it into `master`.

**Before starting a new top-level module, create its branch:**

```sh
git checkout master
git pull
git checkout -b <module-name>    # e.g. topic-engine, codec, session-store
```

Branch names must be short, lowercase, hyphen-separated, and match the module name (e.g. `topic-engine`, `protocol-codec`, `in-memory-store`).

All code changes are committed on this branch — never directly on `master`.

**After the entire top-level module is done:** open a Pull Request `<branch> → master` and merge it.

**Automatic commit after successful completion:**

Once the completion checklist (see below) is fully satisfied — all tests pass **and** test coverage is ≥ 80 % for all changed modules — commit the changes automatically:

```sh
git add -A
git commit -m "<short imperative description of the change>"
```

Do **not** commit if any test fails or if coverage is below 80 %. Fix the issues first, then commit.

**Coverage must be measured and reported before every commit — skipping it is not allowed.**

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

## Directory structure rule — flat source layout

**A directory that contains source-code files (`.h` / `.cpp`) must never have
subdirectories that also contain source-code files.**

Source files must always reside at a single level within their module directory.
The only permitted subdirectory inside a source directory is `test/` (for unit tests).

Valid layout:
```
src/my_module/
    my_class.h
    my_class.cpp
    test/
        my_class_test.cpp
        TEST_SPEC.md
    SPEC.md
```

Invalid layout (source files at two levels):
```
src/my_module/          ← has .h/.cpp files
    my_class.h
    sub/                ← also has .h/.cpp files  ✗
        helper.h
```

If a module grows beyond the file count limit, promote it to a set of sibling
directories under the parent rather than nesting one inside another.

## File count rule

Keep the number of code files per directory (and subdirectory) below 10.
Before adding a new file, check the current count. If a directory is near the
limit, split into sibling subdirectories under the parent — never nest source
directories inside one another.

## Documentation-first: target spec (SPEC.md)

Before writing any code, create or update a `SPEC.md` in the target directory.

- One `SPEC.md` per directory.
- A `SPEC.md` in a parent directory summarises all its subdirectories.
- `SPEC.md` describes: purpose, public API, data structures, behaviour, constraints.
- The code must match `SPEC.md` — it is the implementation guide.
- **`src/SPEC.md` is the top-level index of all modules.** Whenever a new module or sub-module directory is added (or an existing one is extended), update the relevant row in `src/SPEC.md` as part of the same commit. Never leave `src/SPEC.md` out of date.

## Documentation and code stay in sync

- When new insights contradict `SPEC.md`: update `SPEC.md` first, then continue.
- Before a new feature: update `SPEC.md` first, then implement.
- Never let code and documentation diverge.
- Any change to configuration capabilities (new/removed/renamed keys, defaults,
  value ranges, CLI flags, precedence rules, behavior) **must** update
  `README.md` in the same change.

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

## IDE diagnostics — mandatory pre-flight

**Before running the test script**, call `get_errors` on every file that was created or modified. All diagnostics reported by the IDE must be resolved first — regardless of whether they would cause a compiler error.

This includes:
- Short variable/parameter names (< 3 characters)
- Unused includes
- "Can be made static" / "can be `const`" hints
- Cognitive complexity warnings
- Any other clang-tidy or IDE hint

**Exception — Catch2 test functions:** Cognitive complexity diagnostics reported on
`CATCH2_INTERNAL_TEST_*` symbols (i.e. the internal functions generated by Catch2's
`TEST_CASE` macro) may be **ignored**. These are not hand-written functions and their
complexity cannot be reduced at the call-site. All other cognitive complexity warnings
in production code and helper code must still be fixed.

Do **not** treat "it only triggers `-Werror` so it would already fail the build" as a reason to skip this step. IDE hints that do **not** produce compiler errors must still be fixed before the test run.

## Build and verify

See `/build` skill for all commands. Use Python script only — never cmake/ctest/llvm directly.

```sh
python test/run_coverage.py
```

## Completion checklist — mandatory gate

Do not mark any module complete until **every item** below passes.

| # | Criterion | How to verify |
|---|-----------|---------------|
| 1 | **Build clean** | `python run_coverage.py` step 1 exits with 0 errors, 0 warnings |
| 2 | **No compiler warnings** | Guaranteed by `-Werror` — any warning is a build failure |
| 3 | **No linter / IDE warnings** | All clang-tidy diagnostics resolved |
| 4 | **VS Code Problems panel clear** | Use `get_errors` tool on all changed files — zero errors and zero warnings; every diagnostic reported by the IDE must be fixed before marking the task done |
| 5 | **All tests pass** | `python test/run_coverage.py` summary shows `Tests: N/N [OK]`; new tests appear by name in ctest |
| 6 | **Test coverage ≥ 80 %** | `python test/run_coverage.py` summary shows `Threshold: MET` and all production files ≥ 80 % for Regions, Functions, Lines. This step blocks the commit — it may not be skipped. |
| 7 | **SPEC.md is current** | Every touched directory has an accurate SPEC.md |
| 8 | **TEST_SPEC.md is current** | Every test in code has a matching entry; removed tests removed from spec |
| 9 | **Doxygen on all public API** | Every header follows the documentation rules in `/cpp-dev` |

## Completion report — required format

End every implementation with this report. All sections are mandatory.

```
## Completion report — <Module name>

### Build + Tests + Coverage
python test/run_coverage.py -> summary output:

```
Tests      : <N>/<N>  [OK]
File                  Regions  Functions  Lines  Branches
...                   ...      ...        ...    ...
TOTAL                 ...      ...        ...    ...
Threshold  : MET  (all production files >= 80%)
```

Production headers: Regions <X>%, Functions <X>%, Lines <X>%
→ threshold met ✓  /  NOT met ✗ (list files below threshold)

### Docs
SPEC.md current: yes/no
TEST_SPEC.md current: yes/no
Doxygen on all public API: yes/no
```
