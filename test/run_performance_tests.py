#!/usr/bin/env python3
from __future__ import annotations

"""Portable performance test runner for mqtt-broker.

This runner reuses load-focused integration test cases and executes them in
size-based stage windows:
- small: stage 1-3
- middle: stage 3-5
- large: stage 5-10

Output is measurement-oriented. Functional deviations are tracked as measurement
annotations instead of success/fail summaries.
"""

import argparse
import hashlib
import importlib.util
import ipaddress
import json
import os
import socket
import sys
import time
from dataclasses import asdict
from dataclasses import dataclass
from datetime import datetime
from datetime import timezone
from pathlib import Path
from typing import Callable


TEST_DIR = Path(__file__).parent.resolve()
INTEGRATION_TESTS_DIR = TEST_DIR / "integration_tests" / "load"
DEFAULT_RESULTS_FILE = TEST_DIR / "performance_test_results.json"

TRACE_LEVEL_ENV = "MQTT_INTEGRATION_TRACE_LEVEL"
TRACE_MODULES_ENV = "MQTT_INTEGRATION_TRACE_MODULES"
BROKER_OUTPUT_ENV = "MQTT_INTEGRATION_BROKER_OUTPUT"
TARGET_HOST_ENV = "MQTT_INTEGRATION_TARGET_HOST"
TARGET_PORT_ENV = "MQTT_INTEGRATION_TARGET_PORT"
TARGET_WS_PORT_ENV = "MQTT_INTEGRATION_TARGET_WS_PORT"
BROKER_MANAGED_ENV = "MQTT_INTEGRATION_BROKER_MANAGED"

DEFAULT_REMOTE_WS_PORT = 8083


@dataclass(frozen=True)
class RunnerConfig:
    host: str
    port: int
    ws_port: int | None
    timeout_seconds: float


@dataclass(frozen=True)
class StartupOptions:
    trace_level: str | None
    trace_modules: tuple[str, ...]
    broker_output_inherit: bool


@dataclass(frozen=True)
class TestCase:
    name: str
    description: str
    execute: Callable[[RunnerConfig], tuple[bool, str]]


@dataclass(frozen=True)
class StageSpec:
    stage_id: int
    title: str
    description: str
    test_names: tuple[str, ...]


@dataclass(frozen=True)
class Measurement:
    stage_id: int
    stage_title: str
    test_name: str
    duration_seconds: float
    outcome: str
    details: str
    started_at: str
    finished_at: str


@dataclass(frozen=True)
class StageSummary:
    stage_id: int
    stage_title: str
    run_count: int
    total_seconds: float
    average_seconds: float
    deviations: int
    aborted: int


STAGES: tuple[StageSpec, ...] = (
    StageSpec(
        stage_id=1,
        title="connection-baseline",
        description="Baseline connection pressure",
        test_names=("load/connection_load_10_concurrent_all_connack_success",),
    ),
    StageSpec(
        stage_id=2,
        title="connection-storm",
        description="Fast connection storm behavior",
        test_names=("load/connection_storm_100_within_one_second_no_crash",),
    ),
    StageSpec(
        stage_id=3,
        title="qos0-throughput",
        description="Single publisher/subscriber QoS0 throughput",
        test_names=("load/message_throughput_single_pub_sub_qos0_1000_under_5s",),
    ),
    StageSpec(
        stage_id=4,
        title="qos1-throughput",
        description="Single publisher/subscriber QoS1 throughput",
        test_names=("load/message_throughput_single_pub_sub_qos1_1000_acked_and_delivered",),
    ),
    StageSpec(
        stage_id=5,
        title="fanout-medium",
        description="Multiple publishers and subscribers",
        test_names=("load/message_throughput_ten_publishers_ten_subscribers_fanout",),
    ),
    StageSpec(
        stage_id=6,
        title="subscriptions-single-client",
        description="Single client with many subscriptions",
        test_names=("load/subscription_load_single_client_thousand_filters",),
    ),
    StageSpec(
        stage_id=7,
        title="subscriptions-many-clients",
        description="Many clients with subscription fanout",
        test_names=("load/subscription_load_hundred_clients_ten_filters_each",),
    ),
    StageSpec(
        stage_id=8,
        title="combined-progressive",
        description="Progressive combined load stages",
        test_names=("load/combined_progressive_200_connections_with_timeout",),
    ),
    StageSpec(
        stage_id=9,
        title="sustained-persistence",
        description="Retained and offline queue sustained behavior",
        test_names=(
            "load/sustained_load_retained_store_thousand_entries",
            "load/sustained_load_offline_queue_five_hundred",
        ),
    ),
    StageSpec(
        stage_id=10,
        title="sustained-long-run",
        description="Long-running mixed traffic",
        test_names=("load/sustained_load_fifty_clients_sixty_seconds",),
    ),
)


SIZE_STAGE_RANGES: dict[str, tuple[int, int]] = {
    "small": (1, 3),
    "middle": (3, 5),
    "large": (5, 10),
}


def _now_utc_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def _load_test_module(module_path: Path):
    digest = hashlib.sha1(str(module_path).encode("utf-8")).hexdigest()[:12]
    module_name = f"performance_source_{digest}"
    spec = importlib.util.spec_from_file_location(module_name, module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to create import spec for {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _discover_load_tests() -> list[TestCase]:
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
            if name.startswith("load/"):
                discovered.append(TestCase(name=name, description=description, execute=execute))

    discovered.sort(key=lambda test_case: test_case.name)
    return discovered


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
    for family, sock_type, _protocol, _, sockaddr in infos:
        if sock_type != socket.SOCK_STREAM:
            continue
        address = sockaddr[0]
        if any(existing_address == address for _, existing_address in candidates):
            continue
        candidates.append((family, address))

    if not candidates:
        return normalized_host, None

    for _family, candidate_address in candidates:
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


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run mqtt-broker performance tests (load-focused)")
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
    parser.add_argument("--timeout", type=float, default=8.0, help="Test timeout baseline in seconds")
    parser.add_argument(
        "--size",
        choices=["small", "middle", "large"],
        default="small",
        help="Performance size profile (default: small)",
    )
    parser.add_argument(
        "--repeat",
        type=int,
        default=1,
        help="Repeat each stage this many times for measurement stability (default: 1)",
    )
    parser.add_argument(
        "--results-file",
        default=str(DEFAULT_RESULTS_FILE),
        help=f"Measurement output file (default: {DEFAULT_RESULTS_FILE.relative_to(TEST_DIR.parent)})",
    )
    parser.add_argument(
        "--filter",
        action="append",
        default=[],
        help="Filter by stage title, stage id, or test name prefix (repeatable)",
    )
    parser.add_argument("--list", action="store_true", help="List available stages and exit")
    parser.add_argument(
        "--trace-level",
        choices=["none", "error", "warning", "info", "trace"],
        default=None,
        help="Managed broker tracing.global_level override",
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
        help="Broker stdout/stderr handling when managed broker is auto-started (default: discard)",
    )
    return parser.parse_args()


def _tests_by_name(all_tests: list[TestCase]) -> dict[str, TestCase]:
    return {test_case.name: test_case for test_case in all_tests}


def _stage_range_for_size(size_name: str) -> tuple[int, int]:
    if size_name not in SIZE_STAGE_RANGES:
        raise RuntimeError(f"unknown size profile: {size_name}")
    return SIZE_STAGE_RANGES[size_name]


def _matches_user_filter(stage: StageSpec, stage_tests: list[TestCase], raw_filter: str) -> bool:
    candidate = raw_filter.strip().lower()
    if candidate == "":
        return False

    if candidate == str(stage.stage_id):
        return True
    if stage.title.startswith(candidate):
        return True
    for test_case in stage_tests:
        normalized_name = test_case.name.lower()
        if normalized_name == candidate or normalized_name.startswith(candidate):
            return True
    return False


def _select_stages(all_tests: list[TestCase], size_name: str, filters: list[str]) -> list[tuple[StageSpec, list[TestCase]]]:
    stage_start, stage_end = _stage_range_for_size(size_name)
    test_lookup = _tests_by_name(all_tests)

    selected: list[tuple[StageSpec, list[TestCase]]] = []
    for stage in STAGES:
        if stage.stage_id < stage_start or stage.stage_id > stage_end:
            continue

        stage_tests: list[TestCase] = []
        for test_name in stage.test_names:
            test_case = test_lookup.get(test_name)
            if test_case is not None:
                stage_tests.append(test_case)

        if not stage_tests:
            continue

        if filters and not any(_matches_user_filter(stage, stage_tests, user_filter) for user_filter in filters):
            continue

        selected.append((stage, stage_tests))

    return selected


def _print_stage_catalog(all_tests: list[TestCase]) -> None:
    available = _tests_by_name(all_tests)
    print("Available performance stages:")
    for stage in STAGES:
        test_names = [name for name in stage.test_names if name in available]
        if not test_names:
            continue
        print(f"- {stage.stage_id}: {stage.title}")
        print(f"  {stage.description}")
        for test_name in test_names:
            print(f"    - {test_name}")


def _measure_stage_runs(
    selected_stages: list[tuple[StageSpec, list[TestCase]]],
    repeat_count: int,
    config: RunnerConfig,
) -> list[Measurement]:
    measurements: list[Measurement] = []

    for stage, stage_tests in selected_stages:
        print(f"[STAGE] {stage.stage_id} {stage.title} - {stage.description}")
        for repeat_index in range(repeat_count):
            print(f"[STAGE-RUN] {stage.stage_id}.{repeat_index + 1} start")
            for test_case in stage_tests:
                started_at = _now_utc_iso()
                started_perf = time.perf_counter()

                outcome = "measured"
                details = ""
                try:
                    measured, details = test_case.execute(config)
                    if not measured:
                        outcome = "deviation"
                except BaseException as error:  # pylint: disable=broad-except
                    outcome = "aborted"
                    details = str(error)

                duration_seconds = time.perf_counter() - started_perf
                finished_at = _now_utc_iso()
                measurements.append(
                    Measurement(
                        stage_id=stage.stage_id,
                        stage_title=stage.title,
                        test_name=test_case.name,
                        duration_seconds=duration_seconds,
                        outcome=outcome,
                        details=details,
                        started_at=started_at,
                        finished_at=finished_at,
                    )
                )
                print(
                    f"[METRIC] stage={stage.stage_id} run={repeat_index + 1} "
                    f"test={test_case.name} duration={duration_seconds:.3f}s outcome={outcome}"
                )
                if details:
                    print(f"  {details}")

    return measurements


def _build_stage_summaries(measurements: list[Measurement]) -> list[StageSummary]:
    by_stage: dict[tuple[int, str], list[Measurement]] = {}
    for item in measurements:
        key = (item.stage_id, item.stage_title)
        by_stage.setdefault(key, []).append(item)

    summaries: list[StageSummary] = []
    for (stage_id, stage_title), stage_measurements in sorted(by_stage.items(), key=lambda item: item[0][0]):
        run_count = len(stage_measurements)
        total_seconds = sum(metric.duration_seconds for metric in stage_measurements)
        average_seconds = total_seconds / run_count if run_count > 0 else 0.0
        deviations = sum(1 for metric in stage_measurements if metric.outcome == "deviation")
        aborted = sum(1 for metric in stage_measurements if metric.outcome == "aborted")
        summaries.append(
            StageSummary(
                stage_id=stage_id,
                stage_title=stage_title,
                run_count=run_count,
                total_seconds=total_seconds,
                average_seconds=average_seconds,
                deviations=deviations,
                aborted=aborted,
            )
        )

    return summaries


def _save_results(
    result_path: Path,
    args: argparse.Namespace,
    config: RunnerConfig,
    selected_stages: list[tuple[StageSpec, list[TestCase]]],
    measurements: list[Measurement],
    stage_summaries: list[StageSummary],
    run_started_at: str,
    run_finished_at: str,
) -> None:
    result_path.parent.mkdir(parents=True, exist_ok=True)

    payload = {
        "run_started_at": run_started_at,
        "run_finished_at": run_finished_at,
        "arguments": {
            "host": args.host,
            "port": args.port,
            "ws_port": args.ws_port,
            "timeout": args.timeout,
            "size": args.size,
            "repeat": args.repeat,
            "filter": args.filter,
        },
        "resolved_target": asdict(config),
        "selected_stages": [
            {
                "stage_id": stage.stage_id,
                "title": stage.title,
                "description": stage.description,
                "tests": [test_case.name for test_case in stage_tests],
            }
            for stage, stage_tests in selected_stages
        ],
        "summary": [asdict(stage_summary) for stage_summary in stage_summaries],
        "measurements": [asdict(measurement) for measurement in measurements],
    }

    result_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def _print_final_summary(
    args: argparse.Namespace,
    measurements: list[Measurement],
    stage_summaries: list[StageSummary],
    results_path: Path,
) -> None:
    print("")
    print("Performance measurement summary")
    print(f"Size profile: {args.size}")
    print(f"Stage window: {SIZE_STAGE_RANGES[args.size][0]}-{SIZE_STAGE_RANGES[args.size][1]}")
    print(f"Measurement count: {len(measurements)}")

    total_seconds = sum(item.duration_seconds for item in measurements)
    total_deviations = sum(1 for item in measurements if item.outcome == "deviation")
    total_aborted = sum(1 for item in measurements if item.outcome == "aborted")

    throughput_per_minute = 0.0
    if total_seconds > 0.0:
        throughput_per_minute = (len(measurements) / total_seconds) * 60.0

    print(f"Total measured runtime: {total_seconds:.3f}s")
    print(f"Measurement rate: {throughput_per_minute:.3f} tests/min")
    print(f"Annotated deviations: {total_deviations}")
    print(f"Aborted runs: {total_aborted}")
    print("")
    print("Per-stage metrics:")
    for stage_summary in stage_summaries:
        print(
            f"- stage {stage_summary.stage_id:>2} {stage_summary.stage_title:<26} "
            f"runs={stage_summary.run_count:<3} "
            f"total={stage_summary.total_seconds:>8.3f}s "
            f"avg={stage_summary.average_seconds:>8.3f}s "
            f"deviations={stage_summary.deviations:<3} "
            f"aborted={stage_summary.aborted:<3}"
        )

    slowest = sorted(measurements, key=lambda item: item.duration_seconds, reverse=True)[:5]
    if slowest:
        print("")
        print("Top 5 slowest measurements:")
        for item in slowest:
            print(
                f"- stage {item.stage_id:>2} {item.test_name:<66} "
                f"{item.duration_seconds:>8.3f}s ({item.outcome})"
            )

    print("")
    print(f"Result file: {results_path}")


def main() -> int:
    args = _parse_args()
    if args.repeat < 1:
        raise RuntimeError("--repeat must be >= 1")

    all_tests = _discover_load_tests()
    if not all_tests:
        print(f"No performance test files found in {INTEGRATION_TESTS_DIR}")
        return 1

    if args.list:
        _print_stage_catalog(all_tests)
        return 0

    selected_stages = _select_stages(all_tests, args.size, args.filter)
    if not selected_stages:
        print("No performance stages selected. Use --list to inspect available stage ids and test names.")
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

    if resolution_message is not None:
        print(f"[SETUP] {resolution_message}")

    print(
        f"Executing {len(selected_stages)} performance stage(s) "
        f"with size={args.size}, repeat={args.repeat}, target={config.host}:{config.port}"
    )

    run_started_at = _now_utc_iso()
    previous_environment = _apply_integration_trace_environment(startup_options, config)
    measurements: list[Measurement] = []
    try:
        measurements = _measure_stage_runs(
            selected_stages=selected_stages,
            repeat_count=args.repeat,
            config=config,
        )
    finally:
        _restore_integration_trace_environment(previous_environment)

    stage_summaries = _build_stage_summaries(measurements)
    run_finished_at = _now_utc_iso()

    results_path = Path(args.results_file).expanduser().resolve()
    _save_results(
        result_path=results_path,
        args=args,
        config=config,
        selected_stages=selected_stages,
        measurements=measurements,
        stage_summaries=stage_summaries,
        run_started_at=run_started_at,
        run_finished_at=run_finished_at,
    )

    _print_final_summary(
        args=args,
        measurements=measurements,
        stage_summaries=stage_summaries,
        results_path=results_path,
    )

    has_non_measured = any(item.outcome != "measured" for item in measurements)
    return 1 if has_non_measured else 0


if __name__ == "__main__":
    sys.exit(main())