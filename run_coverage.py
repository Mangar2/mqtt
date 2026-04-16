#!/usr/bin/env python3
"""
Coverage analysis script for the mqtt-broker project.

Usage
-----
Full run (build + test + full report):
    python run_coverage.py

Scoped report for one or more modules (skips build+test, uses existing profdata):
    python run_coverage.py --scope src/codec/primitive/ src/codec/properties/

Line-level detail for a single file below threshold:
    python run_coverage.py --show src/codec/properties/properties_codec.cpp
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.resolve()
BUILD_DIR    = PROJECT_ROOT / "build" / "test-coverage"
BINARY       = BUILD_DIR / ("mqtt-broker-tests.exe" if sys.platform == "win32" else "mqtt-broker-tests")
PROFRAW      = BUILD_DIR / "coverage.profraw"
PROFDATA     = BUILD_DIR / "coverage.profdata"
SRC_DIR      = PROJECT_ROOT / "src"


def run(cmd: list[str], env: dict | None = None) -> None:
    """Run a command, inherit stdout/stderr, abort on failure."""
    print(f"\n>>> {' '.join(str(c) for c in cmd)}\n")
    result = subprocess.run(cmd, cwd=PROJECT_ROOT, env=env)
    if result.returncode != 0:
        sys.exit(result.returncode)


def full_run() -> None:
    """Configure, build, run tests, merge profile, print full report."""
    # Step 1 — configure
    run(["cmake", "--preset", "test-coverage"])

    # Step 2 — build
    run(["cmake", "--build", "--preset", "test-coverage"])

    # Step 3 — run binary directly (one complete .profraw)
    env = os.environ.copy()
    env["LLVM_PROFILE_FILE"] = str(PROFRAW)
    run([str(BINARY)], env=env)

    # Step 4 — merge raw data
    run([
        "llvm-profdata", "merge",
        "-sparse", str(PROFRAW),
        "-o", str(PROFDATA),
    ])

    # Step 5 — full report, no Catch2 noise
    run([
        "llvm-cov", "report", str(BINARY),
        f"-instr-profile={PROFDATA}",
        "-ignore-filename-regex=(_deps|catch2|Catch2)",
        str(SRC_DIR),
    ])


def scoped_report(paths: list[str]) -> None:
    """Print a report scoped to the given source paths (requires existing profdata)."""
    _require_profdata()
    run([
        "llvm-cov", "report", str(BINARY),
        f"-instr-profile={PROFDATA}",
        *paths,
    ])


def show_file(path: str) -> None:
    """Print line-level coverage detail for a single file (requires existing profdata)."""
    _require_profdata()
    run([
        "llvm-cov", "show", str(BINARY),
        f"-instr-profile={PROFDATA}",
        path,
    ])


def _require_profdata() -> None:
    if not PROFDATA.exists():
        print(
            f"ERROR: {PROFDATA} not found.\n"
            "Run 'python run_coverage.py' first to build and collect coverage data.",
            file=sys.stderr,
        )
        sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "--scope",
        nargs="+",
        metavar="PATH",
        help="Print a scoped report for the given source paths (reuses existing profdata).",
    )
    group.add_argument(
        "--show",
        metavar="FILE",
        help="Print line-level detail for a single source file (reuses existing profdata).",
    )
    args = parser.parse_args()

    if args.scope:
        scoped_report(args.scope)
    elif args.show:
        show_file(args.show)
    else:
        full_run()


if __name__ == "__main__":
    main()
