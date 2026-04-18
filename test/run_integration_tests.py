#!/usr/bin/env python3
from __future__ import annotations

"""Portable integration test runner for mqtt-broker using mqttx.

Features:
- Hierarchical test names (for example: connect/anonymous).
- Prefix-based filtering via --filter (for example: --filter connect).
- Re-run only previously failed tests via --only-failed.
- Reset persisted state via --reset-state.
- Persist success/failed results to a result file.
"""

import argparse
import hashlib
import importlib.util
import json
import socket
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable


TEST_DIR = Path(__file__).parent.resolve()
PROJECT_ROOT = TEST_DIR.parent
RELEASE_DIR = PROJECT_ROOT / "build" / "release"
BROKER_BINARY = RELEASE_DIR / ("mqtt-broker.exe" if sys.platform == "win32" else "mqtt-broker")
DEFAULT_RESULTS_FILE = TEST_DIR / "integration_test_results.json"
INTEGRATION_TESTS_DIR = TEST_DIR / "integration_tests"


@dataclass(frozen=True)
class RunnerConfig:
    host: str
    port: int
    timeout_seconds: float


@dataclass(frozen=True)
class TestCase:
    name: str
    description: str
    execute: Callable[[RunnerConfig], tuple[bool, str]]


def _is_local_host(hostname: str) -> bool:
    normalized = hostname.strip().lower()
    return normalized in {"localhost", "127.0.0.1", "::1"} or normalized.startswith("127.")


def _broker_reachable(hostname: str, port: int, timeout_seconds: float = 0.5) -> bool:
    try:
        with socket.create_connection((hostname, port), timeout=timeout_seconds):
            return True
    except OSError:
        return False


def _run_or_raise(command: list[str], label: str) -> None:
    completed = subprocess.run(
        command,
        check=False,
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
    )
    if completed.returncode == 0:
        return

    output = "\n".join(part for part in [completed.stdout.strip(), completed.stderr.strip()] if part).strip()
    raise RuntimeError(
        f"{label} failed with exit code {completed.returncode}."
        + (f" Output: {output}" if output else "")
    )


def _ensure_broker_running(config: RunnerConfig) -> tuple[subprocess.Popen[str] | None, list[str]]:
    setup_messages: list[str] = []

    if _broker_reachable(config.host, config.port):
        setup_messages.append(f"Broker already reachable at {config.host}:{config.port}")
        return None, setup_messages

    if not _is_local_host(config.host):
        raise RuntimeError(
            "Broker is not reachable and auto-start is only supported for local hosts "
            f"(requested host: {config.host})."
        )

    setup_messages.append("Broker not reachable, building release broker binary")
    _run_or_raise(["cmake", "--preset", "release"], "cmake configure (release)")
    _run_or_raise(["cmake", "--build", "--preset", "release", "--target", "mqtt-broker"], "cmake build (mqtt-broker)")

    if not BROKER_BINARY.exists():
        raise RuntimeError(f"Expected broker binary not found at {BROKER_BINARY}")

    setup_messages.append(f"Starting broker process: {BROKER_BINARY}")
    broker_process = subprocess.Popen(
        [str(BROKER_BINARY)],
        cwd=PROJECT_ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
    )

    start_deadline = time.monotonic() + max(3.0, config.timeout_seconds)
    while time.monotonic() < start_deadline:
        if _broker_reachable(config.host, config.port, timeout_seconds=0.3):
            setup_messages.append(f"Broker became reachable at {config.host}:{config.port}")
            return broker_process, setup_messages
        if broker_process.poll() is not None:
            raise RuntimeError("Started broker process exited before it became reachable")
        time.sleep(0.2)

    try:
        broker_process.terminate()
        broker_process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        broker_process.kill()
        broker_process.wait(timeout=2)

    raise RuntimeError("Broker did not become reachable after auto-start")


def _stop_broker_if_started(broker_process: subprocess.Popen[str] | None) -> None:
    if broker_process is None:
        return
    if broker_process.poll() is not None:
        return

    broker_process.terminate()
    try:
        broker_process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        broker_process.kill()
        broker_process.wait(timeout=5)


def _now_utc_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def _load_state(path: Path) -> dict:
    if not path.exists():
        return {"results": {}}

    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {"results": {}}


def _save_state(path: Path, state: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _matches_filter(test_name: str, raw_filter: str) -> bool:
    normalized = raw_filter.strip().strip("/")
    if not normalized:
        return False
    return test_name == normalized or test_name.startswith(normalized + "/")


def _select_by_filters(tests: list[TestCase], filters: list[str]) -> list[TestCase]:
    if not filters:
        return list(tests)
    return [
        test
        for test in tests
        if any(_matches_filter(test.name, candidate) for candidate in filters)
    ]


def _load_test_module(module_path: Path):
    digest = hashlib.sha1(str(module_path).encode("utf-8")).hexdigest()[:12]
    module_name = f"integration_test_{digest}"
    spec = importlib.util.spec_from_file_location(module_name, module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to create import spec for {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _discover_tests() -> list[TestCase]:
    if not INTEGRATION_TESTS_DIR.exists():
        return []

    discovered: list[TestCase] = []
    for module_path in sorted(INTEGRATION_TESTS_DIR.rglob("*.py")):
        if module_path.name == "__init__.py":
            continue

        module = _load_test_module(module_path)
        module_cases = getattr(module, "TEST_CASES", None)
        if module_cases is None:
            continue
        if not isinstance(module_cases, list):
            raise RuntimeError(f"TEST_CASES must be a list in {module_path}")

        for case in module_cases:
            if not isinstance(case, dict):
                raise RuntimeError(f"Each test case must be a dict in {module_path}")
            name = case.get("name")
            description = case.get("description")
            execute = case.get("run")
            if not isinstance(name, str) or not isinstance(description, str) or not callable(execute):
                raise RuntimeError(
                    f"Invalid test case format in {module_path}; expected keys name(str), description(str), run(callable)"
                )
            discovered.append(TestCase(name=name, description=description, execute=execute))

    discovered.sort(key=lambda test: test.name)
    return discovered


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run mqtt-broker integration tests via mqttx")
    parser.add_argument("--host", default="127.0.0.1", help="Broker hostname (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=1883, help="Broker TCP port (default: 1883)")
    parser.add_argument("--timeout", type=float, default=8.0, help="mqttx command timeout in seconds")
    parser.add_argument(
        "--results-file",
        default=str(DEFAULT_RESULTS_FILE),
        help=f"Result state file (default: {DEFAULT_RESULTS_FILE.relative_to(TEST_DIR.parent)})",
    )
    parser.add_argument(
        "--filter",
        action="append",
        default=[],
        help="Run tests matching exact name or path prefix (repeatable)",
    )
    parser.add_argument(
        "--only-failed",
        action="store_true",
        help="Re-run only tests that are marked as failed in the result file",
    )
    parser.add_argument(
        "--reset-state",
        action="store_true",
        help="Delete persisted state before selection so all tests can be run again",
    )
    parser.add_argument("--list", action="store_true", help="List all available tests and exit")
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    all_tests = _discover_tests()

    if not all_tests:
        print(f"No test files found in {INTEGRATION_TESTS_DIR}")
        return 1

    if args.list:
        print("Available tests:")
        for test in all_tests:
            print(f"- {test.name}: {test.description}")
        return 0

    results_path = Path(args.results_file).expanduser().resolve()

    if args.reset_state and results_path.exists():
        results_path.unlink()
        print(f"State reset: removed {results_path}")

    state = _load_state(results_path)
    existing_results = state.get("results", {})

    if args.only_failed:
        failed_names = {
            name
            for name, result in existing_results.items()
            if isinstance(result, dict) and result.get("status") == "failed"
        }
        selected_tests = [test for test in all_tests if test.name in failed_names]
        if not selected_tests:
            print("No failed tests to re-run.")
            return 0
    else:
        selected_tests = _select_by_filters(all_tests, args.filter)

    if not selected_tests:
        print("No tests selected. Use --list to inspect available names.")
        return 1

    config = RunnerConfig(host=args.host, port=args.port, timeout_seconds=args.timeout)
    run_started_at = _now_utc_iso()

    broker_process: subprocess.Popen[str] | None = None
    try:
        broker_process, setup_messages = _ensure_broker_running(config)
        for setup_message in setup_messages:
            print(f"[SETUP] {setup_message}")
    except RuntimeError as runtime_error:
        print(f"[FAILED] Broker setup: {runtime_error}")
        for test in selected_tests:
            existing_results[test.name] = {
                "status": "failed",
                "updated_at": _now_utc_iso(),
                "details": f"broker setup failed: {runtime_error}",
            }
        state["results"] = existing_results
        state["last_run_started_at"] = run_started_at
        state["last_run_finished_at"] = _now_utc_iso()
        state["last_run_selection"] = [test.name for test in selected_tests]
        _save_state(results_path, state)
        return 1

    print(f"Running {len(selected_tests)} integration test(s) against {config.host}:{config.port}")

    failures = 0
    try:
        for test in selected_tests:
            ok, details = test.execute(config)
            status_text = "success" if ok else "failed"
            if not ok:
                failures += 1

            print(f"[{status_text.upper()}] {test.name}")
            if details:
                print(f"  {details}")

            existing_results[test.name] = {
                "status": status_text,
                "updated_at": _now_utc_iso(),
                "details": details,
            }
    finally:
        _stop_broker_if_started(broker_process)

    state["results"] = existing_results
    state["last_run_started_at"] = run_started_at
    state["last_run_finished_at"] = _now_utc_iso()
    state["last_run_selection"] = [test.name for test in selected_tests]
    _save_state(results_path, state)

    total = len(selected_tests)
    passed = total - failures
    print(f"Summary: {passed}/{total} success, {failures} failed")
    print(f"Result file: {results_path}")

    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
