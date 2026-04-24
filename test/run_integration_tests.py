#!/usr/bin/env python3
from __future__ import annotations

"""Portable integration test runner for mqtt-broker using mqttx.

Features:
- Hierarchical test names (for example: connect/anonymous).
- Prefix-based filtering via --filter (for example: --filter connect).
- Re-run only previously failed tests via --only-failed.
- Reset persisted state via --reset-state.
- Persist success/failed results to broker-type specific result files (local/remote).
"""

import argparse
import configparser
import hashlib
import importlib.util
import ipaddress
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
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
DEFAULT_BROKER_TEMPLATE_FILE = TEST_DIR / "broker.ws.ini"


@dataclass(frozen=True)
class RunnerConfig:
    host: str
    port: int
    ws_port: int | None
    timeout_seconds: float


@dataclass(frozen=True)
class TestCase:
    name: str
    description: str
    execute: Callable[[RunnerConfig], tuple[bool, str]]


@dataclass(frozen=True)
class StartupOptions:
    trace_level: str | None
    trace_modules: tuple[str, ...]
    broker_output_inherit: bool


TRACE_LEVEL_ENV = "MQTT_INTEGRATION_TRACE_LEVEL"
TRACE_MODULES_ENV = "MQTT_INTEGRATION_TRACE_MODULES"
BROKER_OUTPUT_ENV = "MQTT_INTEGRATION_BROKER_OUTPUT"
TARGET_HOST_ENV = "MQTT_INTEGRATION_TARGET_HOST"
TARGET_PORT_ENV = "MQTT_INTEGRATION_TARGET_PORT"
TARGET_WS_PORT_ENV = "MQTT_INTEGRATION_TARGET_WS_PORT"
BROKER_MANAGED_ENV = "MQTT_INTEGRATION_BROKER_MANAGED"

DEFAULT_REMOTE_WS_PORT = 8083


REQUIREMENT_NUMBER_PREFIX = re.compile(r"^\s*(\d+(?:\.\d+)*)\b")


def _is_local_host(hostname: str) -> bool:
    normalized = hostname.strip().lower()
    return normalized in {"localhost", "127.0.0.1", "::1"} or normalized.startswith("127.")


def _broker_reachable(hostname: str, port: int, timeout_seconds: float = 0.5) -> bool:
    try:
        with socket.create_connection((hostname, port), timeout=timeout_seconds):
            return True
    except OSError:
        return False


def _is_ip_literal(hostname: str) -> bool:
    try:
        ipaddress.ip_address(hostname.strip())
        return True
    except ValueError:
        return False


def _resolve_runner_target_host(hostname: str, port: int) -> tuple[str, str | None]:
    normalized_host = hostname.strip()
    if not normalized_host or _is_ip_literal(normalized_host):
        return normalized_host, None

    try:
        infos = socket.getaddrinfo(normalized_host, port, type=socket.SOCK_STREAM)
    except OSError:
        return normalized_host, None

    candidates: list[tuple[int, str]] = []
    for family, sock_type, protocol, _, sockaddr in infos:
        if sock_type != socket.SOCK_STREAM:
            continue
        address = sockaddr[0]
        if any(existing_address == address for _, existing_address in candidates):
            continue
        candidates.append((family, address))

    if not candidates:
        return normalized_host, None

    for _, candidate_address in candidates:
        if _broker_reachable(candidate_address, port, timeout_seconds=0.4):
            if candidate_address != normalized_host:
                return candidate_address, f"Resolved host {normalized_host} -> {candidate_address}"
            return candidate_address, None

    ipv4_candidate = next(
        (candidate_address for family, candidate_address in candidates if family == socket.AF_INET),
        None,
    )
    if ipv4_candidate is not None and ipv4_candidate != normalized_host:
        return ipv4_candidate, (
            f"Resolved host {normalized_host} -> {ipv4_candidate} "
            "(IPv4 fallback to avoid IPv6-only connect failures)"
        )

    return normalized_host, None


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


def _ensure_broker_running(
    config: RunnerConfig,
    startup_options: StartupOptions,
) -> tuple[subprocess.Popen[str] | None, list[str]]:
    setup_messages: list[str] = []

    if _broker_reachable(config.host, config.port):
        if startup_options.trace_level is not None or startup_options.trace_modules:
            raise RuntimeError(
                "Broker already reachable; trace options require managed broker startup. "
                "Stop existing broker on target host/port or use a different --port."
            )
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

    generated_config_path = _create_startup_config(config, startup_options)
    command = [str(BROKER_BINARY)]
    if generated_config_path is not None:
        command.append(str(generated_config_path))
        setup_messages.append(f"Using generated broker config: {generated_config_path}")

    setup_messages.append(f"Starting broker process: {BROKER_BINARY}")
    stdout_target = None if startup_options.broker_output_inherit else subprocess.DEVNULL
    stderr_target = None if startup_options.broker_output_inherit else subprocess.DEVNULL
    broker_process = subprocess.Popen(
        command,
        cwd=PROJECT_ROOT,
        stdout=stdout_target,
        stderr=stderr_target,
        text=True,
    )
    setattr(
        broker_process,
        "_integration_runner_generated_config_dir",
        str(generated_config_path.parent) if generated_config_path is not None else "",
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
        _cleanup_generated_broker_config(broker_process)
        return

    broker_process.terminate()
    try:
        broker_process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        broker_process.kill()
        broker_process.wait(timeout=5)
    finally:
        _cleanup_generated_broker_config(broker_process)


def _format_ini_value(value: object) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def _create_startup_config(config: RunnerConfig, startup_options: StartupOptions) -> Path | None:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    if DEFAULT_BROKER_TEMPLATE_FILE.exists():
        parser.read(DEFAULT_BROKER_TEMPLATE_FILE, encoding="utf-8")

    if not parser.has_section("network"):
        parser.add_section("network")
    parser.set("network", "mqtt_port", _format_ini_value(config.port))
    parser.set("network", "ws_port", _format_ini_value(0))

    if startup_options.trace_level is not None or startup_options.trace_modules:
        if not parser.has_section("tracing"):
            parser.add_section("tracing")
        if startup_options.trace_level is not None:
            parser.set("tracing", "global_level", startup_options.trace_level)
        if startup_options.trace_modules:
            parser.set("tracing", "trace_modules", ",".join(startup_options.trace_modules))

    config_dir = Path(tempfile.mkdtemp(prefix="mqtt-integration-runner-config-"))
    config_path = config_dir / "broker.ini"

    lines: list[str] = []
    for section_name in parser.sections():
        lines.append(f"[{section_name}]")
        for key_name, value in parser.items(section_name):
            lines.append(f"{key_name} = {value}")
        lines.append("")

    if lines and lines[-1] == "":
        lines.pop()

    config_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return config_path


def _cleanup_generated_broker_config(process: subprocess.Popen[str]) -> None:
    generated_dir = getattr(process, "_integration_runner_generated_config_dir", "")
    if not generated_dir:
        return

    try:
        shutil.rmtree(Path(generated_dir), ignore_errors=True)
    finally:
        setattr(process, "_integration_runner_generated_config_dir", "")


def _apply_integration_trace_environment(
    startup_options: StartupOptions,
    config: RunnerConfig,
) -> dict[str, str | None]:
    previous_values: dict[str, str | None] = {
        TRACE_LEVEL_ENV: os.environ.get(TRACE_LEVEL_ENV),
        TRACE_MODULES_ENV: os.environ.get(TRACE_MODULES_ENV),
        BROKER_OUTPUT_ENV: os.environ.get(BROKER_OUTPUT_ENV),
        TARGET_HOST_ENV: os.environ.get(TARGET_HOST_ENV),
        TARGET_PORT_ENV: os.environ.get(TARGET_PORT_ENV),
        TARGET_WS_PORT_ENV: os.environ.get(TARGET_WS_PORT_ENV),
        BROKER_MANAGED_ENV: os.environ.get(BROKER_MANAGED_ENV),
    }

    if startup_options.trace_level is None:
        os.environ.pop(TRACE_LEVEL_ENV, None)
    else:
        os.environ[TRACE_LEVEL_ENV] = startup_options.trace_level

    if not startup_options.trace_modules:
        os.environ.pop(TRACE_MODULES_ENV, None)
    else:
        os.environ[TRACE_MODULES_ENV] = ",".join(startup_options.trace_modules)

    os.environ[BROKER_OUTPUT_ENV] = (
        "inherit" if startup_options.broker_output_inherit else "discard"
    )
    os.environ[TARGET_HOST_ENV] = config.host
    os.environ[TARGET_PORT_ENV] = str(config.port)
    if config.ws_port is None:
        os.environ.pop(TARGET_WS_PORT_ENV, None)
    else:
        os.environ[TARGET_WS_PORT_ENV] = str(config.ws_port)

    return previous_values


def _restore_integration_trace_environment(previous_values: dict[str, str | None]) -> None:
    for variable_name, previous_value in previous_values.items():
        if previous_value is None:
            os.environ.pop(variable_name, None)
        else:
            os.environ[variable_name] = previous_value


def _now_utc_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def _default_results_file_for_type(result_type: str) -> Path:
    return DEFAULT_RESULTS_FILE.with_name(f"{DEFAULT_RESULTS_FILE.stem}.{result_type}{DEFAULT_RESULTS_FILE.suffix}")


def _resolve_results_path(raw_results_file: str | None, result_type: str) -> Path:
    if raw_results_file is None:
        return _default_results_file_for_type(result_type).resolve()
    return Path(raw_results_file).expanduser().resolve()


def _load_state(path: Path) -> dict:
    if not path.exists():
        return {"results": {}}

    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {"results": {}}


def _save_state(path: Path, state: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")


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


def _test_sort_key(test: TestCase) -> tuple:
    number_match = REQUIREMENT_NUMBER_PREFIX.match(test.description)
    if number_match:
        number_parts = tuple(int(part) for part in number_match.group(1).split("."))
        return (0, number_parts, test.name)

    return (1, test.name)


def _requirement_number_from_description(description: str) -> str | None:
    number_match = REQUIREMENT_NUMBER_PREFIX.match(description)
    if number_match is None:
        return None
    return number_match.group(1)


def _ordered_results(results: dict, tests: list[TestCase]) -> dict:
    known_test_names = {test.name for test in tests}
    ordered_known_test_names = [test.name for test in tests if test.name in results]
    ordered_unknown_test_names = sorted(name for name in results if name not in known_test_names)

    ordered: dict = {}
    for test_name in ordered_known_test_names:
        ordered[test_name] = results[test_name]
    for test_name in ordered_unknown_test_names:
        ordered[test_name] = results[test_name]

    return ordered


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

    discovered.sort(key=_test_sort_key)
    return discovered


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run mqtt-broker integration tests via mqttx")
    parser.add_argument("--host", default="127.0.0.1", help="Broker hostname (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=1883, help="Broker TCP port (default: 1883)")
    parser.add_argument(
        "--ws-port",
        type=int,
        default=None,
        help=(
            "Broker WebSocket port for external target. "
            "When omitted with non-local --host, defaults to 8083"
        ),
    )
    parser.add_argument("--timeout", type=float, default=8.0, help="mqttx command timeout in seconds")
    parser.add_argument(
        "--results-file",
        default=None,
        help=(
            "Result state file override. "
            f"Default uses broker type specific files: "
            f"{_default_results_file_for_type('local').relative_to(TEST_DIR.parent)} and "
            f"{_default_results_file_for_type('remote').relative_to(TEST_DIR.parent)}"
        ),
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
    parser.add_argument(
        "--trace-level",
        choices=["none", "error", "warning", "info", "trace"],
        default=None,
        help="Start managed broker with tracing.global_level override",
    )
    parser.add_argument(
        "--trace-module",
        action="append",
        default=[],
        help="Enable tracing.trace_modules entry (repeatable)",
    )
    parser.add_argument(
        "--broker-output",
        choices=["discard", "inherit"],
        default="discard",
        help="Broker stdout/stderr handling when auto-started (default: discard)",
    )
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

    result_type = "local" if _is_local_host(args.host) else "remote"
    results_path = _resolve_results_path(args.results_file, result_type)

    if args.reset_state and results_path.exists():
        results_path.unlink()
        print(f"State reset: removed {results_path}")

    if args.results_file is None:
        print(f"[SETUP] Using broker-type result file ({result_type}): {results_path}")
    else:
        print(f"[SETUP] Using explicit result file: {results_path}")

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

    resolved_host, resolution_message = _resolve_runner_target_host(args.host, args.port)
    resolved_ws_port: int | None = args.ws_port
    if resolved_ws_port is None and not _is_local_host(args.host):
        resolved_ws_port = DEFAULT_REMOTE_WS_PORT

    config = RunnerConfig(
        host=resolved_host,
        port=args.port,
        ws_port=resolved_ws_port,
        timeout_seconds=args.timeout,
    )
    startup_options = StartupOptions(
        trace_level=args.trace_level,
        trace_modules=tuple(args.trace_module),
        broker_output_inherit=(args.broker_output == "inherit"),
    )
    previous_trace_environment = _apply_integration_trace_environment(
        startup_options,
        config,
    )
    run_started_at = _now_utc_iso()

    if resolution_message is not None:
        print(f"[SETUP] {resolution_message}")

    broker_process: subprocess.Popen[str] | None = None
    try:
        broker_process, setup_messages = _ensure_broker_running(config, startup_options)
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
        state["results"] = _ordered_results(existing_results, all_tests)
        state["last_run_started_at"] = run_started_at
        state["last_run_finished_at"] = _now_utc_iso()
        state["last_run_selection"] = [test.name for test in selected_tests]
        _save_state(results_path, state)
        return 1

    broker_is_managed = _is_local_host(config.host)
    os.environ[BROKER_MANAGED_ENV] = "1" if broker_is_managed else "0"
    if not broker_is_managed:
        print("[SETUP] Running only remote-safe test cases (managed-broker tests may be skipped)")

    print(f"Running {len(selected_tests)} integration test(s) against {config.host}:{config.port}")

    failures = 0
    skipped = 0
    try:
        for test in selected_tests:
            try:
                ok, details = test.execute(config)
                status_text = "success" if ok else "failed"
                if not ok:
                    failures += 1
            except BaseException as error:  # pylint: disable=broad-except
                if error.__class__.__name__ == "ManagedBrokerRequired":
                    status_text = "skipped"
                    skipped += 1
                    details = str(error)
                else:
                    raise

            if status_text == "skipped":
                requirement_number = _requirement_number_from_description(test.description)
                if requirement_number is not None:
                    if not details:
                        details = f"{requirement_number} skipped"
                    elif not details.startswith(f"{requirement_number} "):
                        details = f"{requirement_number} skipped: {details}"

            if status_text == "failed":
                requirement_number = _requirement_number_from_description(test.description)
                if requirement_number is not None:
                    if not details:
                        details = f"{requirement_number} failed"
                    elif not details.startswith(f"{requirement_number} "):
                        details = f"{requirement_number} failed: {details}"

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
        _restore_integration_trace_environment(previous_trace_environment)

    state["results"] = _ordered_results(existing_results, all_tests)
    state["last_run_started_at"] = run_started_at
    state["last_run_finished_at"] = _now_utc_iso()
    state["last_run_selection"] = [test.name for test in selected_tests]
    _save_state(results_path, state)

    total = len(selected_tests)
    passed = total - failures
    passed = total - failures - skipped
    print(f"Summary: {passed}/{total} success, {failures} failed, {skipped} skipped")
    print(f"Result file: {results_path}")

    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
