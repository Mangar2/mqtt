#!/usr/bin/env python3
from __future__ import annotations

import argparse
from dataclasses import dataclass
import importlib.util
from pathlib import Path
import sys
import time
from typing import Callable


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


ROOT = Path(__file__).resolve().parent


def _load_test_cases() -> list[TestCase]:
    test_cases: list[TestCase] = []
    for module_path in sorted(ROOT.rglob("*.py")):
        if module_path.name == "run_msgstore_integration_tests.py":
            continue

        spec = importlib.util.spec_from_file_location(f"yaha_test_{module_path.stem}", module_path)
        if spec is None or spec.loader is None:
            raise RuntimeError(f"unable to load test module {module_path}")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        entries = getattr(module, "TEST_CASES", None)
        if entries is None:
            continue
        for entry in entries:
            test_cases.append(
                TestCase(
                    name=str(entry["name"]),
                    description=str(entry["description"]),
                    execute=entry["run"],
                )
            )

    test_cases.sort(key=lambda item: item.name)
    return test_cases


def _apply_filter(test_cases: list[TestCase], prefix_filter: str | None) -> list[TestCase]:
    if not prefix_filter:
        return test_cases
    normalized = prefix_filter.strip().lower()
    return [test_case for test_case in test_cases if test_case.name.lower().startswith(normalized)]


def main() -> None:
    parser = argparse.ArgumentParser(description="Run YAHA msgstore integration tests.")
    parser.add_argument("--host", default="127.0.0.1", help="broker host")
    parser.add_argument("--port", type=int, default=1883, help="broker port (for reporting only)")
    parser.add_argument("--timeout", type=float, default=8.0, help="default timeout seconds")
    parser.add_argument("--filter", default="", help="prefix filter for test names")
    parser.add_argument("--list", action="store_true", help="list tests and exit")
    args = parser.parse_args()

    all_tests = _load_test_cases()
    if not all_tests:
        print("No tests found under test/yaha")
        return

    selected = _apply_filter(all_tests, args.filter)

    if args.list:
        for test_case in selected:
            print(f"{test_case.name} - {test_case.description}")
        return

    if not selected:
        print(f"No tests matched filter '{args.filter}'")
        sys.exit(1)

    config = RunnerConfig(host=args.host, port=args.port, timeout_seconds=args.timeout)
    print(f"Running {len(selected)} YAHA integration test(s)")

    passed = 0
    failed = 0
    started_at = time.monotonic()

    for index, test_case in enumerate(selected, start=1):
        ok, detail = test_case.execute(config)
        if ok:
            passed += 1
            print(f"[PASSED] [{index}/{len(selected)}] {test_case.name}")
            print(f"  {detail}")
        else:
            failed += 1
            print(f"[FAILED] [{index}/{len(selected)}] {test_case.name}")
            print(f"  {detail}")

    duration = time.monotonic() - started_at
    print(f"Summary: {passed}/{len(selected)} passed, {failed} failed in {duration:.1f}s")

    if failed > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
