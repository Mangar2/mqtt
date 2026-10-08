"""Repeat the two flaky client tests and report duration and failures.

Usage: python3 spec/bug/flaky-client-tests/repro.py [runs] [binary]
PASS when every run succeeds within the 2s runner limit, FAIL otherwise.
"""
import subprocess
import sys
import time
from pathlib import Path

TESTS = [
    "value_service_monitoring_filesystem_watch_is_ignored",
    "serial_device_runtime_internal_trace_logs_keepalive_and_status_reply_flow",
]
RUNNER_LIMIT_SECONDS = 2.0
ROOT = Path(__file__).resolve().parents[3]


def main() -> int:
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    binary = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "build" / "test-coverage" / "yahabroker-tests"
    failed = False
    for test_name in TESTS:
        durations = []
        failures = []
        for run_index in range(runs):
            started = time.monotonic()
            try:
                result = subprocess.run(
                    [str(binary), test_name, "--reporter", "compact", "--colour-mode", "none"],
                    capture_output=True, text=True, timeout=10, cwd=ROOT)
                code = result.returncode
                output = result.stdout + result.stderr
            except subprocess.TimeoutExpired as exc:
                code = "hang"
                output = str(exc.stdout or "")
            elapsed = time.monotonic() - started
            durations.append(elapsed)
            if code != 0 or elapsed > RUNNER_LIMIT_SECONDS:
                failures.append((run_index, code, round(elapsed, 2), output.strip().splitlines()[-3:]))
        durations.sort()
        print(f"{test_name}: runs={runs} median={durations[len(durations) // 2]:.3f}s "
              f"max={durations[-1]:.3f}s failures={len(failures)}")
        for failure in failures[:5]:
            print("   ", failure)
        failed = failed or bool(failures)
    print("FAIL" if failed else "PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
