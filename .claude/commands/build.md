Build and coverage are now split by scope and must be run separately from project root.

## ABSOLUTE PRE-RUN GATE

Before any compile, build, coverage run, or test execution, run `get_errors` for every changed file and resolve all reported problems.

Hard rule: zero problems only. No warning, no hint, no exception.

Never start build/test commands while any `get_errors` problem is still open.

Broker scope:

```sh
python test/run_coverage_broker.py
```

Client scopes (YAHA plus generic client):

```sh
python test/run_coverage_clients.py
```

Run both commands for full validation.
Stops on first failure. Logs: test/run_broker.log and test/run_clients.log
Never call cmake/ctest/llvm directly.
