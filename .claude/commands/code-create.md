Full workflow for creating or extending code in this project. Follow every step in order.

## Branch and commit workflow

For all Pull Request operations, use `/github-pr` and execute its steps exactly.

One branch per top-level module (e.g. `3. Topic Engine`). All sub-modules (3.1, 3.2, …) are committed on the same branch. When the entire top-level module is complete, open a Pull Request and merge it into `master`.

Before starting a new top-level module, create its branch:

```sh
git checkout master
git pull
git checkout -b <module-name>    # e.g. topic-engine, codec, session-store
```

Branch names must be short, lowercase, hyphen-separated, and match the module name (e.g. `topic-engine`, `protocol-codec`, `in-memory-store`).

All code changes are committed on this branch — never directly on `master`.

After the entire top-level module is done: open a Pull Request `<branch> → master` and merge it.

Automatic commit after successful completion:

Once the completion checklist (see below) is fully satisfied — all tests pass and test coverage is ≥ 80 % for all changed modules — commit the changes automatically:

```sh
git add -A
git commit -m "<short imperative description of the change>"
```

Do not commit if any test fails or if coverage is below 80 %. Fix the issues first, then commit.

Coverage must be measured and reported before every commit — skipping it is not allowed.

## Build structure — read CMakeLists.txt first

Before writing any `.cpp` file in a new module, read `CMakeLists.txt` to verify
the glob patterns and link structure are compatible. Never guess.

Current build structure (verified in CMakeLists.txt):

Glob src/*.cpp excluding /test/ and main.cpp
Target yahabroker and yahabroker-tests
Notes MQTT_LIB_SOURCES feeds both

Glob src/*_test.cpp any depth
Target yahabroker-tests only

Glob src/main.cpp
Target yahabroker only
Notes excluded from tests via list FILTER main.cpp

Both targets share `src/` on their include path, so headers are included as
`codec/…`, `data_model/…`, etc.

Other facts (do not read CMakePresets.json):
- Test framework: Catch2 v3 (`Catch2::Catch2WithMain`), already linked.
- Compile flags: `-Wall -Wextra -Wpedantic -Werror` on both targets.
- CMake re-globs at every build invocation — no manual file registration needed.

## Directory structure rule — flat source layout

A directory that contains source-code files (`.h` / `.cpp`) must never have
subdirectories that also contain source-code files.

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
- `src/SPEC.md` is the top-level index of all modules. Whenever a new module or sub-module directory is added (or an existing one is extended), update the relevant row in `src/SPEC.md` as part of the same commit. Never leave `src/SPEC.md` out of date.

## Documentation and code stay in sync — NO EXCEPTIONS

SPEC.md is documentation, not a plan. It must always reflect the current state of the code.

**Every code change — no matter how small — requires updating SPEC.md in the same step.**
This includes: modifying existing behaviour, adding or removing methods, changing locking strategy,
renaming symbols, changing thread safety guarantees, changing error handling, or any other
observable behaviour difference. There is no exception to this rule.

- When new insights contradict `SPEC.md`: update `SPEC.md` first, then continue.
- Before a new feature: update `SPEC.md` first, then implement.
- After any code change: verify every affected `SPEC.md` is accurate before marking the task done.
- Never let code and documentation diverge — a SPEC.md that does not match the code is a bug.
- Any change to configuration capabilities (new/removed/renamed keys, defaults,
  value ranges, CLI flags, precedence rules, behavior) must update
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

Before running the test script, call `get_errors` on every file that was created or modified. All diagnostics reported by the IDE must be resolved first — regardless of whether they would cause a compiler error.

This includes:
- Short variable/parameter names (< 3 characters)
- Unused includes
- "Can be made static" / "can be `const`" hints
- Cognitive complexity warnings
- Any other clang-tidy or IDE hint

Exception — Catch2 test functions: Cognitive complexity diagnostics reported on
`CATCH2_INTERNAL_TEST_*` symbols (i.e. the internal functions generated by Catch2's
`TEST_CASE` macro) may be ignored. These are not hand-written functions and their
complexity cannot be reduced at the call-site. All other cognitive complexity warnings
in production code and helper code must still be fixed.

Do not treat "it only triggers `-Werror` so it would already fail the build" as a reason to skip this step. IDE hints that do not produce compiler errors must still be fixed before the test run.

## Build and verify

See `/build` skill for all commands. Use Python script only — never cmake/ctest/llvm directly.

Before marking any code change complete, run the scope-matching coverage script from the project root:

- broker scope: `python3 test/run_coverage_broker.py`
- client scope: `python3 test/run_coverage_clients.py`
- mixed or unclear scope: both scripts

```sh
python test/run_coverage_broker.py
python test/run_coverage_clients.py
```

Completion checklist mandatory gate

Do not mark any module complete until every item passes.

1 Build clean
Verify python test/run_coverage_broker.py and python test/run_coverage_clients.py step 1 exit with 0 errors and 0 warnings.

2 No compiler warnings
Build uses -Werror so any warning is a build failure.

3 No linter IDE warnings
All clang tidy diagnostics resolved.

4 VS Code Problems panel clear
Use get_errors on all changed files. Zero errors and zero warnings. Fix every IDE diagnostic before done.

5 All tests pass
Both script summaries must show Tests N slash N OK for their scopes.

6 Test coverage at least 80 percent
Both script summaries must show Threshold MET and all production files in their scopes at least 80 percent for Regions Functions Lines. This blocks commit.

7 SPEC.md current — MANDATORY, NO EXCEPTIONS
Every directory whose code was touched has an accurate, up-to-date SPEC.md.
SPEC.md must reflect the code as it actually is after the change — not as it was planned or as it was before.
A completion checklist that passes with an outdated SPEC.md is invalid. This item blocks commit without exception.

8 TEST_SPEC.md current
Every test in code has matching entry. Removed tests removed from spec.

9 Doxygen on all header declarations
Every declaration in every .h file follows cpp-dev. Every method documented. No exceptions.

## Completion report — required format

End every implementation with this report. All sections are mandatory.

```
## Completion report — <Module name>

### Build + Tests + Coverage
python test/run_coverage_broker.py and python test/run_coverage_clients.py -> summary output:

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
Doxygen on all header declarations: yes/no
```

## Mandatory completion action get_errors per file

Before declaring any development task as complete, run IDE diagnostics with
`get_errors` for every changed file individually.

This is mandatory no exceptions.

Required execution sequence:

1. Build the exact list of changed files.
2. Call `get_errors` with one file path at a time, not only folder-wide checks.
3. Fix all reported diagnostics.
4. Re-run `get_errors` again for each changed file until all are clean.
5. Send explicit completion feedback to the user that this check was executed.

Mandatory user feedback format at completion:

- `get_errors per changed file executed: yes`
- `files checked: <comma-separated list>`
- `remaining diagnostics: none` or `remaining diagnostics: <file + short summary>`

Do not mark the task done without this feedback.
