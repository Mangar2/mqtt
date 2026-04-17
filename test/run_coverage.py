#!/usr/bin/env python3
"""
Build, test and coverage script for the mqtt-broker project.

Usage
-----
Full workflow (compile debug, run tests, compile coverage, measure coverage):
    python run_coverage.py

Scoped coverage report for one or more paths (reuses existing profdata):
    python run_coverage.py --scope src/topic/trie/

Line-level detail for a single file (reuses existing profdata):
    python run_coverage.py --show src/topic/trie/subscription_trie.cpp

All output is also written to test/run.log -- read it only when debugging failures.
Always run from the project root or from the test/ directory.
"""

import argparse
import os
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

# Script lives in <project>/test/ — project root is one level up.
TEST_DIR     = Path(__file__).parent.resolve()
PROJECT_ROOT = TEST_DIR.parent
DEBUG_DIR    = PROJECT_ROOT / "build" / "debug"
COV_DIR      = PROJECT_ROOT / "build" / "test-coverage"   # cmake preset output (fixed)
DEBUG_BINARY = DEBUG_DIR / ("mqtt-broker-tests.exe" if sys.platform == "win32" else "mqtt-broker-tests")
COV_BINARY   = COV_DIR  / ("mqtt-broker-tests.exe" if sys.platform == "win32" else "mqtt-broker-tests")
PROFRAW      = TEST_DIR / "coverage.profraw"
PROFDATA     = TEST_DIR / "coverage.profdata"
SRC_DIR      = PROJECT_ROOT / "src"
LOG_FILE     = TEST_DIR / "run.log"

THRESHOLD = 90.0  # minimum coverage percent


# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

_log_fh = None


def _open_log() -> None:
    global _log_fh
    LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
    _log_fh = open(LOG_FILE, "w", encoding="utf-8")
    _log_fh.write(f"=== run_coverage.py  {datetime.now().isoformat(timespec='seconds')} ===\n\n")


def _log(text: str) -> None:
    if _log_fh:
        _log_fh.write(text + "\n")
        _log_fh.flush()


def _close_log() -> None:
    if _log_fh:
        _log_fh.close()


# ---------------------------------------------------------------------------
# Command execution
# ---------------------------------------------------------------------------

def _run_captured(cmd: list[str], env: dict | None = None) -> tuple[int, str]:
    """Run a command, capture combined stdout+stderr, write everything to log."""
    _log(f">>> {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(
        cmd,
        cwd=PROJECT_ROOT,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    _log(result.stdout)
    return result.returncode, result.stdout


def _run_or_die(label: str, cmd: list[str], env: dict | None = None) -> str:
    """Run a command; on failure print a concise error summary and exit."""
    rc, output = _run_captured(cmd, env)
    if rc != 0:
        print(f"\n[FAILED] {label}")
        print(f"  command : {' '.join(str(c) for c in cmd)}")
        print(f"  exit    : {rc}")
        error_lines = [
            line for line in output.splitlines()
            if re.search(r"\berror\b|\bfatal\b|FAILED|could not|undefined", line, re.IGNORECASE)
        ]
        if error_lines:
            print("\n  --- relevant output ---")
            for line in error_lines[:40]:
                print(f"  {line}")
        else:
            print("\n  --- last lines of output ---")
            for line in output.splitlines()[-30:]:
                print(f"  {line}")
        print(f"\n  Full log: {LOG_FILE}")
        _close_log()
        sys.exit(rc)
    return output


# ---------------------------------------------------------------------------
# Build / test / coverage steps
# ---------------------------------------------------------------------------

def step_build_debug() -> None:
    _run_or_die("cmake build (debug)", ["cmake", "--build", "--preset", "debug"])


def step_run_tests() -> str:
    """Run ctest in debug mode. Returns raw output for summary parsing."""
    rc, output = _run_captured(["ctest", "--preset", "debug", "--output-on-failure"])
    _log(f"ctest exit code: {rc}")
    if rc != 0:
        print("\n[FAILED] Unit tests")
        failed = [
            line.strip() for line in output.splitlines()
            if re.search(r"FAILED|Failed", line)
        ]
        print("\n  --- failed tests ---")
        for line in failed[:30]:
            print(f"  {line}")
        print(f"\n  Full log: {LOG_FILE}")
        _close_log()
        sys.exit(rc)
    return output


def step_build_coverage() -> None:
    _run_or_die("cmake build (test-coverage)", ["cmake", "--build", "--preset", "test-coverage"])


def step_collect_coverage() -> None:
    env = os.environ.copy()
    env["LLVM_PROFILE_FILE"] = str(PROFRAW)
    _run_or_die("run test binary (coverage)", [str(COV_BINARY)], env=env)
    _run_or_die("llvm-profdata merge", [
        "llvm-profdata", "merge", "-sparse", str(PROFRAW), "-o", str(PROFDATA),
    ])


def step_coverage_report(paths: list[str] | None = None) -> str:
    targets = paths if paths else [str(SRC_DIR)]
    return _run_or_die("llvm-cov report", [
        "llvm-cov", "report", str(COV_BINARY),
        f"-instr-profile={PROFDATA}",
        "-ignore-filename-regex=(_deps|catch2|Catch2)",
        *targets,
    ])


def step_coverage_show(path: str) -> str:
    return _run_or_die("llvm-cov show", [
        "llvm-cov", "show", str(COV_BINARY),
        f"-instr-profile={PROFDATA}",
        path,
    ])


# ---------------------------------------------------------------------------
# Summary formatting
# ---------------------------------------------------------------------------

def _parse_test_summary(ctest_output: str) -> tuple[int, int]:
    for line in reversed(ctest_output.splitlines()):
        # e.g. "100% tests passed, 0 tests failed out of 338"
        match = re.search(r"(\d+)%\s+tests\s+passed.*?out of\s+(\d+)", line)
        if match:
            pct   = int(match.group(1))
            total = int(match.group(2))
            return round(total * pct / 100), total
    return 0, 0


def _parse_coverage_rows(cov_output: str) -> list[dict]:
    """Return one row per production source file (no test files, no Catch2)."""
    rows = []
    for line in cov_output.splitlines():
        # Accept only lines that are source files (.cpp or .h)
        if not re.search(r"\.(cpp|h)\b", line):
            continue
        # Skip Catch2 / build artefacts
        if re.search(r"_deps|catch2|Catch2|build[/\\]", line):
            continue
        # Skip test files
        if "_test.cpp" in line or "TEST_SPEC" in line:
            continue
        parts = line.split()
        if len(parts) < 10:
            continue
        try:
            rows.append({
                "file":      Path(parts[0]).name,
                "regions":   parts[3],
                "functions": parts[6],
                "lines":     parts[9],
                "branches":  parts[12] if len(parts) > 12 else "-",
            })
        except (IndexError, ValueError):
            continue
    return rows


def _parse_total_row(cov_output: str) -> dict | None:
    for line in cov_output.splitlines():
        if line.strip().startswith("TOTAL"):
            parts = line.split()
            try:
                return {
                    "file":      "TOTAL",
                    "regions":   parts[3],
                    "functions": parts[6],
                    "lines":     parts[9],
                    "branches":  parts[12] if len(parts) > 12 else "-",
                }
            except IndexError:
                return None
    return None


def _pct(pct_str: str) -> float:
    try:
        return float(pct_str.rstrip("%"))
    except ValueError:
        return 100.0


def _files_below_threshold(rows: list[dict]) -> list[str]:
    bad = []
    for row in rows:
        for key in ("regions", "functions", "lines"):
            if _pct(row[key]) < THRESHOLD:
                bad.append(f"{row['file']} ({key} {row[key]})")
                break
    return bad


def print_summary(ctest_output: str, cov_output: str) -> None:
    passed, total = _parse_test_summary(ctest_output)
    rows      = _parse_coverage_rows(cov_output)
    total_row = _parse_total_row(cov_output)
    below     = _files_below_threshold(rows)

    W = 62
    print()
    print("=" * W)
    print("  SUMMARY")
    print("=" * W)

    test_status = "OK" if total > 0 and passed == total else "FAIL"
    print(f"  Tests      : {passed}/{total}  [{test_status}]")

    if rows:
        col_w = max((len(r["file"]) for r in rows), default=4)
        if total_row:
            col_w = max(col_w, len(total_row["file"]))
        col_w = max(col_w, 4)
        fmt    = f"  {{:<{col_w}}}  {{:>8}}  {{:>10}}  {{:>6}}  {{:>9}}"
        sep    = "  " + "-" * (col_w + 40)
        print()
        print(f"  Coverage (threshold {THRESHOLD:.0f}%)")
        print(fmt.format("File", "Regions", "Functions", "Lines", "Branches"))
        print(sep)
        for row in rows:
            print(fmt.format(row["file"], row["regions"], row["functions"], row["lines"], row["branches"]))
        if total_row:
            print(sep)
            print(fmt.format(total_row["file"], total_row["regions"], total_row["functions"], total_row["lines"], total_row["branches"]))

    print()
    if below:
        print("  Threshold  : NOT MET")
        for item in below:
            print(f"    - {item}")
        print(f"\n  Use: python run_coverage.py --show src/.../file.cpp")
    else:
        print(f"  Threshold  : MET  (all production files >= {THRESHOLD:.0f}%)")

    print("=" * W)
    print(f"  Log        : {LOG_FILE}")
    print("=" * W)
    print()


# ---------------------------------------------------------------------------
# Entry points
# ---------------------------------------------------------------------------

def full_run() -> None:
    _open_log()

    print("[1/4] Building debug binary ...")
    step_build_debug()
    print("      OK")

    print("[2/4] Running unit tests ...")
    ctest_output = step_run_tests()
    passed, total = _parse_test_summary(ctest_output)
    print(f"      {passed}/{total} passed")

    print("[3/4] Building coverage binary ...")
    step_build_coverage()
    print("      OK")

    print("[4/4] Measuring coverage ...")
    step_collect_coverage()
    cov_output = step_coverage_report()
    print("      OK")

    print_summary(ctest_output, cov_output)
    _close_log()


def scoped_run(paths: list[str]) -> None:
    _require_profdata()
    _open_log()
    cov_output = step_coverage_report(paths)
    print_summary("100% tests passed, 0 tests failed out of 0", cov_output)
    _close_log()


def show_run(path: str) -> None:
    _require_profdata()
    _open_log()
    print(step_coverage_show(path))
    _close_log()


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
    grp = parser.add_mutually_exclusive_group()
    grp.add_argument(
        "--scope", nargs="+", metavar="PATH",
        help="Scoped coverage report only (reuses existing profdata; no rebuild).",
    )
    grp.add_argument(
        "--show", metavar="FILE",
        help="Line-level detail for one source file (reuses existing profdata).",
    )
    args = parser.parse_args()

    if args.scope:
        scoped_run(args.scope)
    elif args.show:
        show_run(args.show)
    else:
        full_run()


if __name__ == "__main__":
    main()
