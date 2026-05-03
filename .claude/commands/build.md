Build and coverage are now split by scope and must be run separately from project root.

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
