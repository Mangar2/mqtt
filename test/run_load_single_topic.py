#!/usr/bin/env python3
from __future__ import annotations

"""Single-topic load runner for mqtt-broker.

Start with one focused scenario: repeated MQTT connect/disconnect at a
configured target rate for a fixed duration.
"""

import argparse
import statistics
import time
from dataclasses import dataclass
from typing import Callable

from integration_tests.helpers.mqtt_client import MqttClient


@dataclass(frozen=True)
class RunnerConfig:
    host: str
    port: int
    rate_per_second: int
    duration_seconds: float
    timeout_seconds: float
    keepalive_seconds: int


@dataclass
class RunStats:
    scheduled: int = 0
    attempted: int = 0
    succeeded: int = 0
    failed: int = 0
    total_connect_disconnect_seconds: float = 0.0
    latencies_seconds: list[float] | None = None
    sample_errors: list[str] | None = None

    def __post_init__(self) -> None:
        if self.latencies_seconds is None:
            self.latencies_seconds = []
        if self.sample_errors is None:
            self.sample_errors = []


@dataclass(frozen=True)
class TestCase:
    name: str
    description: str
    run: Callable[[RunnerConfig], RunStats]


def _run_connect_disconnect_case(config: RunnerConfig) -> RunStats:
    stats = RunStats()
    period_seconds = 1.0 / float(config.rate_per_second)

    start_time = time.monotonic()
    end_time = start_time + config.duration_seconds
    next_tick = start_time

    while next_tick < end_time:
        now = time.monotonic()
        if now < next_tick:
            time.sleep(next_tick - now)

        scheduled_start = next_tick
        next_tick += period_seconds
        stats.scheduled += 1

        if time.monotonic() >= end_time:
            break

        stats.attempted += 1
        op_start = time.monotonic()

        try:
            client_id = f"load-connect-{stats.attempted}"
            with MqttClient(client_id=client_id, timeout_seconds=config.timeout_seconds) as client:
                connack = client.connect(
                    host=config.host,
                    port=config.port,
                    clean_start=True,
                    keepalive=config.keepalive_seconds,
                )
                if connack.reason_code != 0:
                    raise RuntimeError(f"CONNACK reason code {connack.reason_code}")
                client.disconnect()
            stats.succeeded += 1
        except Exception as error:
            stats.failed += 1
            if len(stats.sample_errors) < 20:
                offset_seconds = time.monotonic() - start_time
                stats.sample_errors.append(f"t={offset_seconds:.3f}s: {error}")

        op_elapsed = time.monotonic() - op_start
        stats.total_connect_disconnect_seconds += op_elapsed
        stats.latencies_seconds.append(op_elapsed)

        # If one operation took longer than one period, do not create a burst
        # of immediate retries. Skip missed ticks and continue with steady pace.
        current_time = time.monotonic()
        if current_time - scheduled_start > period_seconds:
            next_tick = current_time + period_seconds

    return stats


TEST_CASES: dict[str, TestCase] = {
    "connect-disconnect": TestCase(
        name="connect-disconnect",
        description="Repeated MQTT CONNECT + DISCONNECT with fixed rate",
        run=_run_connect_disconnect_case,
    ),
}


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run single-topic mqtt-broker load tests with shared rate/duration parameters.",
    )
    parser.add_argument(
        "--case",
        choices=sorted(TEST_CASES.keys()),
        default="connect-disconnect",
        help="Focused load test case to run",
    )
    parser.add_argument("--host", default="127.0.0.1", help="Broker host")
    parser.add_argument("--port", type=int, default=1883, help="Broker TCP port")
    parser.add_argument(
        "-n",
        "--rate",
        type=int,
        required=True,
        help="Target operations per second for all load jobs",
    )
    parser.add_argument(
        "-s",
        "--duration",
        type=float,
        required=True,
        help="Test duration in seconds for all load jobs",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=3.0,
        help="Per connect/disconnect operation timeout in seconds",
    )
    parser.add_argument(
        "--keepalive",
        type=int,
        default=30,
        help="MQTT keepalive value in seconds",
    )
    return parser


def _validate_args(args: argparse.Namespace) -> None:
    if args.rate <= 0:
        raise SystemExit("rate must be > 0")
    if args.duration <= 0.0:
        raise SystemExit("duration must be > 0")
    if args.timeout <= 0.0:
        raise SystemExit("timeout must be > 0")
    if args.port <= 0 or args.port > 65535:
        raise SystemExit("port must be in range 1..65535")
    if args.keepalive <= 0:
        raise SystemExit("keepalive must be > 0")


def _print_summary(config: RunnerConfig, case: TestCase, stats: RunStats, wall_time: float) -> None:
    attempted_rate = stats.attempted / wall_time if wall_time > 0.0 else 0.0
    success_rate = stats.succeeded / wall_time if wall_time > 0.0 else 0.0

    print("=== Load Test Summary ===")
    print(f"case            : {case.name}")
    print(f"description     : {case.description}")
    print(f"host            : {config.host}")
    print(f"port            : {config.port}")
    print(f"rate target     : {config.rate_per_second}/s")
    print(f"duration target : {config.duration_seconds:.3f}s")
    print(f"wall time       : {wall_time:.3f}s")
    print(f"scheduled       : {stats.scheduled}")
    print(f"attempted       : {stats.attempted}")
    print(f"succeeded       : {stats.succeeded}")
    print(f"failed          : {stats.failed}")
    print(f"attempted rate  : {attempted_rate:.2f}/s")
    print(f"success rate    : {success_rate:.2f}/s")

    if stats.latencies_seconds:
        lat_ms = [value * 1000.0 for value in stats.latencies_seconds]
        p50 = statistics.median(lat_ms)
        p95_index = max(0, int(0.95 * len(lat_ms)) - 1)
        sorted_lat_ms = sorted(lat_ms)
        p95 = sorted_lat_ms[p95_index]
        avg = sum(lat_ms) / len(lat_ms)
        print(f"latency avg     : {avg:.2f} ms")
        print(f"latency p50     : {p50:.2f} ms")
        print(f"latency p95     : {p95:.2f} ms")

    if stats.sample_errors:
        print("errors (up to 20):")
        for entry in stats.sample_errors:
            print(f"  - {entry}")


def main() -> int:
    parser = _build_parser()
    args = parser.parse_args()
    _validate_args(args)

    config = RunnerConfig(
        host=args.host,
        port=args.port,
        rate_per_second=args.rate,
        duration_seconds=args.duration,
        timeout_seconds=args.timeout,
        keepalive_seconds=args.keepalive,
    )

    case = TEST_CASES[args.case]

    print("=== Load Test Start ===")
    print(f"case        : {case.name}")
    print(f"host:port   : {config.host}:{config.port}")
    print(f"rate n      : {config.rate_per_second}/s")
    print(f"duration s  : {config.duration_seconds:.3f}s")
    print(f"timeout     : {config.timeout_seconds:.3f}s")

    wall_start = time.monotonic()
    stats = case.run(config)
    wall_elapsed = time.monotonic() - wall_start

    _print_summary(config, case, stats, wall_elapsed)

    return 0 if stats.failed == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
