Build and coverage are now split by scope and must be run separately from project root.

## ABSOLUTE PRE-RUN GATE

Before any compile, build, coverage run, or test execution, run `get_errors` for every changed file and resolve all reported problems.

Hard rule: zero problems only. No warning, no hint, no exception.

Never start build/test commands while any `get_errors` problem is still open.

Broker scope (only when working on broker code):

```sh
python test/run_coverage_broker.py
```

YAHA client scope (when working on YAHA clients):

```sh
python test/run_coverage_clients.py
```

Run both commands only when both scopes were touched or the scope is unclear.
Stops on first failure. Logs: test/run_broker.log and test/run_clients.log
Never call cmake/ctest/llvm directly.

## Hard enforcement for focused verification

- `ctest` is forbidden for local verification, focused checks, and quick reruns.
- If only a module needs validation, use the Python script with `--scope` instead of `ctest -R`.

Examples:

```sh
python test/run_coverage_broker.py --scope src/<module>/
python test/run_coverage_clients.py --scope src/<module>/
```
