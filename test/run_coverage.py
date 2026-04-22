#!/usr/bin/env python3
from __future__ import annotations

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
import json
import os
import re
import selectors
import shutil
import subprocess
import sys
import time
import uuid
from datetime import datetime
from pathlib import Path

# Script lives in <project>/test/ — project root is one level up.
TEST_DIR     = Path(__file__).parent.resolve()
PROJECT_ROOT = TEST_DIR.parent
DEBUG_DIR    = PROJECT_ROOT / "build" / "debug"
COV_DIR      = PROJECT_ROOT / "build" / "test-coverage"   # cmake preset output (fixed)
DEBUG_BINARY = DEBUG_DIR / ("mqtt-broker-tests.exe" if sys.platform == "win32" else "mqtt-broker-tests")
COV_BINARY   = COV_DIR  / ("mqtt-broker-tests.exe" if sys.platform == "win32" else "mqtt-broker-tests")
PROFDATA     = TEST_DIR / "coverage.profdata"
PROFRAW_GLOB = "coverage-*.profraw"
COVERAGE_ARTIFACTS_DIR = TEST_DIR / "coverage"
SRC_DIR      = PROJECT_ROOT / "src"
LOG_FILE     = TEST_DIR / "run.log"
SUMMARY_FILE = TEST_DIR / "run_coverage.summary.json"

THRESHOLD = 80.0  # minimum coverage percent

# Unit-test execution safety guards to avoid indefinite waits in step 2.
CTEST_PER_TEST_TIMEOUT_SECONDS = int(os.environ.get("MQTT_CTEST_TIMEOUT", "120"))
CTEST_TOTAL_TIMEOUT_SECONDS = int(os.environ.get("MQTT_CTEST_TOTAL_TIMEOUT", "1800"))
CTEST_NO_OUTPUT_TIMEOUT_SECONDS = int(
    os.environ.get("MQTT_CTEST_NO_OUTPUT_TIMEOUT", "300")
)
CTEST_STOP_ON_FAILURE = os.environ.get("MQTT_CTEST_STOP_ON_FAILURE", "1") not in {
    "0", "false", "False"
}

# Coverage-binary execution guard for step 4.
# This intentionally protects the Python script from waiting forever when
# the coverage binary makes no forward progress.
COVERAGE_TEST_TIMEOUT_SECONDS = int(
    os.environ.get("MQTT_COVERAGE_TEST_TIMEOUT", "600")
)


# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

_log_fh = None
_run_id = None


def _open_log() -> None:
    global _log_fh, _run_id
    LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
    _run_id = uuid.uuid4().hex[:8]
    _log_fh = open(LOG_FILE, "w", encoding="utf-8")
    _log_fh.write(
        f"=== run_coverage.py run_id={_run_id} "
        f"{datetime.now().isoformat(timespec='seconds')} ===\n\n"
    )


def _log(text: str) -> None:
    if _log_fh:
        _log_fh.write(text + "\n")
        _log_fh.flush()


def _close_log() -> None:
    if _log_fh:
        _log_fh.close()


def _write_summary(summary: dict) -> None:
    SUMMARY_FILE.parent.mkdir(parents=True, exist_ok=True)
    with open(SUMMARY_FILE, "w", encoding="utf-8") as fh:
        json.dump(summary, fh, indent=2, sort_keys=True)
        fh.write("\n")


def _emit_done_marker(status: str, exit_code: int) -> None:
    print(f"RUN_COVERAGE_DONE status={status} exit={exit_code}")


# ---------------------------------------------------------------------------
# Command execution
# ---------------------------------------------------------------------------

def _run_captured(
    cmd: list[str], env: dict | None = None, timeout_seconds: int | None = None
) -> tuple[int, str]:
    """Run a command, capture combined stdout+stderr, write everything to log."""
    _log(f">>> {' '.join(str(c) for c in cmd)}")
    try:
        result = subprocess.run(
            cmd,
            cwd=PROJECT_ROOT,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout_seconds,
        )
        _log(result.stdout)
        return result.returncode, result.stdout
    except subprocess.TimeoutExpired as exc:
        captured = (exc.stdout or "")
        _log(captured)
        _log(
            f"[TIMEOUT] command exceeded {timeout_seconds}s: "
            f"{' '.join(str(c) for c in cmd)}"
        )
        return 124, captured


def _run_or_die(
    label: str,
    cmd: list[str],
    env: dict | None = None,
    timeout_seconds: int | None = None,
) -> str:
    """Run a command; on failure print a concise error summary and exit."""
    rc, output = _run_captured(cmd, env, timeout_seconds=timeout_seconds)
    if rc != 0:
        print(f"\n[FAILED] {label}")
        print(f"  command : {' '.join(str(c) for c in cmd)}")
        print(f"  exit    : {rc}")
        if rc == 124 and timeout_seconds is not None:
            print(f"  timeout : {timeout_seconds}s")
            print(
                "  hint    : process made no usable progress; "
                "investigate run.log around step [4/4]"
            )
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


def _run_captured_live(
    cmd: list[str],
    env: dict | None = None,
    total_timeout_seconds: int | None = None,
    no_output_timeout_seconds: int | None = None,
) -> tuple[int, str]:
    """Run a command with live capture and watchdogs for hangs."""
    _log(f">>> {' '.join(str(c) for c in cmd)}")
    process = subprocess.Popen(
        cmd,
        cwd=PROJECT_ROOT,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
    )
    assert process.stdout is not None

    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)

    started_at = time.monotonic()
    last_output_at = started_at
    last_heartbeat_at = started_at
    output_chunks: list[str] = []
    timeout_triggered = False

    try:
        while True:
            now = time.monotonic()

            if (
                total_timeout_seconds is not None
                and now - started_at > total_timeout_seconds
            ):
                process.kill()
                timeout_triggered = True
                timeout_note = (
                    f"\n[TIMEOUT] total timeout exceeded: {total_timeout_seconds}s\n"
                )
                output_chunks.append(timeout_note)
                _log(timeout_note.rstrip("\n"))
                break

            if (
                no_output_timeout_seconds is not None
                and now - last_output_at > no_output_timeout_seconds
            ):
                process.kill()
                timeout_triggered = True
                timeout_note = (
                    "\n[TIMEOUT] no ctest output for "
                    f"{no_output_timeout_seconds}s; process terminated\n"
                )
                output_chunks.append(timeout_note)
                _log(timeout_note.rstrip("\n"))
                break

            events = selector.select(timeout=1.0)
            if events:
                for _key, _mask in events:
                    line = process.stdout.readline()
                    if line:
                        output_chunks.append(line)
                        _log(line.rstrip("\n"))
                        last_output_at = time.monotonic()
                    elif process.poll() is not None:
                        break
            elif now - last_heartbeat_at >= 30.0:
                heartbeat = (
                    "[ctest] still running "
                    f"({int(now - started_at)}s elapsed, last output "
                    f"{int(now - last_output_at)}s ago)"
                )
                print(heartbeat)
                _log(heartbeat)
                last_heartbeat_at = now

            if process.poll() is not None:
                break
    finally:
        try:
            remainder = process.stdout.read()
            if remainder:
                output_chunks.append(remainder)
                _log(remainder.rstrip("\n"))
        except Exception:
            pass
        selector.close()

    rc = process.poll()
    if timeout_triggered:
        rc = 124
    elif rc is None:
        rc = 124
    output = "".join(output_chunks)
    return rc, output


def _ensure_configured(preset: str) -> None:
    _run_or_die(f"cmake configure ({preset})", ["cmake", "--preset", preset])


def _resolve_llvm_tool(tool_name: str) -> list[str]:
    direct_path = shutil.which(tool_name)
    if direct_path is not None:
        return [tool_name]

    xcrun_path = shutil.which("xcrun")
    if xcrun_path is not None:
        rc, _ = _run_captured(["xcrun", "--find", tool_name])
        if rc == 0:
            return ["xcrun", tool_name]

    print(
        f"\n[FAILED] missing tool: {tool_name}\n"
        f"  Install LLVM tools or ensure '{tool_name}' is on PATH.\n"
        f"  On macOS, Xcode Command Line Tools usually provide it via xcrun.",
        file=sys.stderr,
    )
    _close_log()
    sys.exit(1)


# ---------------------------------------------------------------------------
# Build / test / coverage steps
# ---------------------------------------------------------------------------

def step_build_debug() -> None:
    _ensure_configured("debug")
    _run_or_die("cmake build (debug)", ["cmake", "--build", "--preset", "debug"])


def step_run_tests() -> str:
    """Run ctest in debug mode. Returns raw output for summary parsing."""
    command = [
        "ctest",
        "--preset",
        "debug",
        "--output-on-failure",
        "--timeout",
        str(CTEST_PER_TEST_TIMEOUT_SECONDS),
    ]
    if CTEST_STOP_ON_FAILURE:
        command.append("--stop-on-failure")

    rc, output = _run_captured_live(
        command,
        total_timeout_seconds=CTEST_TOTAL_TIMEOUT_SECONDS,
        no_output_timeout_seconds=CTEST_NO_OUTPUT_TIMEOUT_SECONDS,
    )
    _log(f"ctest exit code: {rc}")
    if rc != 0:
        print("\n[FAILED] Unit tests")
        if rc == 124:
            print(
                f"\n  --- timeout ---\n"
                f"  ctest exceeded total timeout of {CTEST_TOTAL_TIMEOUT_SECONDS}s"
            )
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
    _ensure_configured("test-coverage")
    _run_or_die("cmake build (test-coverage)", ["cmake", "--build", "--preset", "test-coverage"])


def step_collect_coverage() -> None:
    llvm_profdata_cmd = _resolve_llvm_tool("llvm-profdata")

    COVERAGE_ARTIFACTS_DIR.mkdir(parents=True, exist_ok=True)

    # Remove stale coverage raw files from previous runs.
    for raw_path in COVERAGE_ARTIFACTS_DIR.glob(PROFRAW_GLOB):
        raw_path.unlink(missing_ok=True)
    # Also clean legacy raw files in test/ from older script versions.
    for raw_path in TEST_DIR.glob(PROFRAW_GLOB):
        raw_path.unlink(missing_ok=True)

    env = os.environ.copy()
    env["LLVM_PROFILE_FILE"] = str(COVERAGE_ARTIFACTS_DIR / "coverage-%p.profraw")

    # Run coverage tests through ctest so each discovered test keeps explicit
    # timeout boundaries and failures identify the concrete test case.
    coverage_ctest_cmd = [
        "ctest",
        "--preset",
        "test-coverage",
        "--output-on-failure",
        "--timeout",
        str(CTEST_PER_TEST_TIMEOUT_SECONDS),
    ]
    if CTEST_STOP_ON_FAILURE:
        coverage_ctest_cmd.append("--stop-on-failure")

    rc, output = _run_captured_live(
        coverage_ctest_cmd,
        env=env,
        total_timeout_seconds=min(
            CTEST_TOTAL_TIMEOUT_SECONDS, COVERAGE_TEST_TIMEOUT_SECONDS
        ),
        no_output_timeout_seconds=CTEST_NO_OUTPUT_TIMEOUT_SECONDS,
    )
    _log(f"coverage ctest exit code: {rc}")
    if rc != 0:
        print("\n[FAILED] Coverage test execution")
        if rc == 124:
            print(
                "\n  --- timeout ---\n"
                "  Coverage ctest exceeded configured timeout guard"
            )
        failed = [
            line.strip() for line in output.splitlines()
            if re.search(r"FAILED|Failed|Timeout", line)
        ]
        print("\n  --- failed tests ---")
        for line in failed[:40]:
            print(f"  {line}")
        print(f"\n  Full log: {LOG_FILE}")
        _close_log()
        sys.exit(rc)

    profraw_files = sorted(COVERAGE_ARTIFACTS_DIR.glob(PROFRAW_GLOB))
    if not profraw_files:
        print(
            "\n[FAILED] coverage profile collection\n"
            "  No .profraw files were generated by coverage test run.\n"
            f"  Expected pattern: {COVERAGE_ARTIFACTS_DIR / PROFRAW_GLOB}",
            file=sys.stderr,
        )
        _close_log()
        sys.exit(1)

    merge_cmd = [
        *llvm_profdata_cmd,
        "merge",
        "-sparse",
        *(str(path) for path in profraw_files),
        "-o",
        str(PROFDATA),
    ]
    _run_or_die("llvm-profdata merge", merge_cmd)


def step_coverage_report(paths: list[str] | None = None) -> str:
    llvm_cov_cmd = _resolve_llvm_tool("llvm-cov")
    targets = paths if paths else [str(SRC_DIR)]
    return _run_or_die("llvm-cov report", [
        *llvm_cov_cmd, "report", str(COV_BINARY),
        f"-instr-profile={PROFDATA}",
        "-ignore-filename-regex=(_deps|catch2|Catch2)",
        *targets,
    ])


def step_coverage_show(path: str) -> str:
    llvm_cov_cmd = _resolve_llvm_tool("llvm-cov")
    return _run_or_die("llvm-cov show", [
        *llvm_cov_cmd, "show", str(COV_BINARY),
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


def _parse_summary_data(ctest_output: str, cov_output: str) -> dict:
    passed, total = _parse_test_summary(ctest_output)
    rows = _parse_coverage_rows(cov_output)
    total_row = _parse_total_row(cov_output)
    below = _files_below_threshold(rows)
    return {
        "tests_passed": passed,
        "tests_total": total,
        "threshold_met": len(below) == 0,
        "files_below_threshold": below,
        "coverage_total": total_row,
    }


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

def full_run() -> dict:
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
    return _parse_summary_data(ctest_output, cov_output)


def unit_only_run() -> dict:
    _open_log()

    print("[1/2] Building debug binary ...")
    step_build_debug()
    print("      OK")

    print("[2/2] Running unit tests ...")
    ctest_output = step_run_tests()
    passed, total = _parse_test_summary(ctest_output)
    print(f"      {passed}/{total} passed")

    print()
    print("=" * 62)
    print("  SUMMARY")
    print("=" * 62)
    print(f"  Tests      : {passed}/{total}  [{'OK' if passed == total and total > 0 else 'FAIL'}]")
    print("  Mode       : unit-only")
    print("=" * 62)
    print(f"  Log        : {LOG_FILE}")
    print("=" * 62)
    print()

    _close_log()
    return {
        "tests_passed": passed,
        "tests_total": total,
        "threshold_met": None,
        "files_below_threshold": [],
        "coverage_total": None,
    }


def coverage_only_run() -> dict:
    _open_log()

    print("[1/2] Building coverage binary ...")
    step_build_coverage()
    print("      OK")

    print("[2/2] Measuring coverage ...")
    step_collect_coverage()
    cov_output = step_coverage_report()
    print("      OK")

    print_summary("100% tests passed, 0 tests failed out of 0", cov_output)
    _close_log()
    return _parse_summary_data("100% tests passed, 0 tests failed out of 0", cov_output)


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
    grp.add_argument(
        "--unit-only", action="store_true",
        help="Build debug target and run unit tests only (no coverage collection).",
    )
    grp.add_argument(
        "--coverage-only", action="store_true",
        help="Build coverage target and collect coverage only.",
    )
    args = parser.parse_args()
    started_at = datetime.now()
    exit_code = 0
    status = "ok"
    summary: dict = {
        "run_id": None,
        "mode": "full",
        "started_at": started_at.isoformat(timespec="seconds"),
        "finished_at": None,
        "duration_s": None,
        "exit_code": None,
        "status": None,
        "tests_passed": None,
        "tests_total": None,
        "threshold_met": None,
        "files_below_threshold": [],
        "coverage_total": None,
        "log_file": str(LOG_FILE),
    }

    try:
        if args.scope:
            summary["mode"] = "scope"
            scoped_run(args.scope)
        elif args.show:
            summary["mode"] = "show"
            show_run(args.show)
        elif args.unit_only:
            summary["mode"] = "unit-only"
            summary.update(unit_only_run())
        elif args.coverage_only:
            summary["mode"] = "coverage-only"
            summary.update(coverage_only_run())
        else:
            summary["mode"] = "full"
            summary.update(full_run())
    except SystemExit as exc:
        exit_code = int(exc.code) if isinstance(exc.code, int) else 1
        status = "failed"
    except Exception:
        exit_code = 1
        status = "failed"
        raise
    finally:
        finished_at = datetime.now()
        summary["run_id"] = _run_id
        summary["finished_at"] = finished_at.isoformat(timespec="seconds")
        summary["duration_s"] = round((finished_at - started_at).total_seconds(), 3)
        summary["exit_code"] = exit_code
        summary["status"] = status
        _write_summary(summary)
        _emit_done_marker(status, exit_code)

    if exit_code != 0:
        sys.exit(exit_code)


if __name__ == "__main__":
    main()
