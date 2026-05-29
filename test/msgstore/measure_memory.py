#!/usr/bin/env python3
"""Measure MessageStore memory growth with controlled JSONL datasets.

This script runs yahamsgstoreclient in synchronous test mode twice:
1) reference run with an empty input file
2) workload run with the selected workload file

Each run uses deterministic handshake phases and measures RSS:
- before sending 'load' command
- after 'ready_for_end' (load completed)
- peak sampled RSS during the run
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import re
import shutil
import subprocess
import time
import select
from pathlib import Path


SAVE_FILE_RE = re.compile(r"\bfile=([^\s]+)")


@dataclasses.dataclass(frozen=True)
class Scenario:
    name: str
    message_count: int
    topic_count: int
    reason_count: int
    reason0_step_ms: int
    fixed_value: float | None


SCENARIOS = {
    "topics_1000_one_message": Scenario(
        name="topics_1000_one_message",
        message_count=1000,
        topic_count=1000,
        reason_count=1,
        reason0_step_ms=1000,
        fixed_value=34.0,
    ),
    "one_topic_50000": Scenario(
        name="one_topic_50000",
        message_count=50000,
        topic_count=1,
        reason_count=1,
        reason0_step_ms=1000,
        fixed_value=34.0,
    ),
    "one_topic_50000_reason4": Scenario(
        name="one_topic_50000_reason4",
        message_count=50000,
        topic_count=1,
        reason_count=4,
        reason0_step_ms=1000,
        fixed_value=34.0,
    ),
    "one_topic_100000": Scenario(
        name="one_topic_100000",
        message_count=100000,
        topic_count=1,
        reason_count=1,
        reason0_step_ms=1000,
        fixed_value=34.0,
    ),
    "topics_1000_100000": Scenario(
        name="topics_1000_100000",
        message_count=100000,
        topic_count=1000,
        reason_count=1,
        reason0_step_ms=1000,
        fixed_value=34.0,
    ),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Measure msgstore memory growth (reference run vs workload run).")
    parser.add_argument(
        "--binary",
        type=Path,
        default=Path("build/release/yahamsgstoreclient"),
        help="Path to yahamsgstoreclient binary.",
    )
    parser.add_argument(
        "--scenario",
        choices=sorted(SCENARIOS.keys()),
        default="topics_1000_one_message",
        help="Built-in load scenario to generate.",
    )
    parser.add_argument(
        "--input-file",
        type=Path,
        default=None,
        help="Optional existing JSONL input file. If set, generator is skipped.",
    )
    parser.add_argument(
        "--tmp-dir",
        type=Path,
        default=Path("test/msgstore/.tmp/mem_benchmark"),
        help="Temporary directory for generated files and run working dir.",
    )
    parser.add_argument(
        "--keep-artifacts",
        action="store_true",
        help="Keep generated temp files for debugging.",
    )
    parser.add_argument(
        "--sample-interval-ms",
        type=int,
        default=5,
        help="RSS sampling interval in milliseconds (default: 5).",
    )
    return parser.parse_args()


def ensure_binary(path: Path) -> Path:
    resolved = path.resolve()
    if not resolved.exists():
        raise FileNotFoundError(f"binary not found: {resolved}")
    if not os.access(resolved, os.X_OK):
        raise PermissionError(f"binary is not executable: {resolved}")
    return resolved


def run_generate_messages(repo_root: Path, output_file: Path, scenario: Scenario) -> None:
    generator = repo_root / "test/msgstore/generate_messages.py"
    cmd = [
        "python3",
        str(generator),
        "--message-count",
        str(scenario.message_count),
        "--topic-count",
        str(scenario.topic_count),
        "--reason-count",
        str(scenario.reason_count),
        "--reason0-step-ms",
        str(scenario.reason0_step_ms),
        "--output-file",
        str(output_file),
        "--seed",
        "42",
    ]
    if scenario.fixed_value is not None:
        cmd += ["--fixed-value", str(scenario.fixed_value)]

    completed = subprocess.run(cmd, cwd=repo_root, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RuntimeError(
            "generate_messages failed:\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )


def sample_rss_kib(pid: int) -> int | None:
    completed = subprocess.run(
        ["ps", "-o", "rss=", "-p", str(pid)],
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        return None
    text = completed.stdout.strip()
    if not text:
        return None
    try:
        value = int(text)
        if value <= 0:
            return None
        return value
    except ValueError:
        return None


def extract_snapshot_size(stdout_text: str, run_dir: Path) -> int | None:
    for line in stdout_text.splitlines():
        if not line.startswith("test.save"):
            continue
        match = SAVE_FILE_RE.search(line)
        if not match:
            continue
        snapshot_rel = Path(match.group(1))
        snapshot_abs = run_dir / snapshot_rel
        if snapshot_abs.exists():
            return snapshot_abs.stat().st_size
    return None


def bytes_to_kib(value_bytes: int | None) -> int | None:
    if value_bytes is None:
        return None
    return (value_bytes + 1023) // 1024


def wait_for_phase(
    process: subprocess.Popen[str],
    expected_phase: str,
    sample_interval_ms: int,
    captured_stdout_lines: list[str],
    current_peak_rss_kib: int | None,
) -> int | None:
    if process.stdout is None:
        raise RuntimeError("process stdout is not available")

    sample_sleep = max(1, sample_interval_ms) / 1000.0
    expected_token = f"test.handshake phase={expected_phase}"

    while True:
        if process.poll() is not None:
            raise RuntimeError(
                f"process exited before phase '{expected_phase}' was observed"
            )

        rss_kib = sample_rss_kib(process.pid)
        if rss_kib is not None and (current_peak_rss_kib is None or rss_kib > current_peak_rss_kib):
            current_peak_rss_kib = rss_kib

        readable, _, _ = select.select([process.stdout], [], [], sample_sleep)
        if not readable:
            continue

        line = process.stdout.readline()
        if line == "":
            raise RuntimeError(
                f"stdout closed before phase '{expected_phase}' was observed"
            )
        normalized = line.rstrip("\n")
        captured_stdout_lines.append(normalized)
        if expected_token in normalized:
            return current_peak_rss_kib


def run_measured(
    binary: Path,
    input_file: Path,
    run_dir: Path,
    sample_interval_ms: int,
) -> tuple[int | None, int | None, int | None, int | None, str, str]:
    cmd = [str(binary), "--test", str(input_file), "--test-handshake"]
    process = subprocess.Popen(
        cmd,
        cwd=run_dir,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    captured_stdout_lines: list[str] = []
    peak_rss_kib: int | None = None
    peak_rss_kib = wait_for_phase(
        process,
        "ready_for_start",
        sample_interval_ms,
        captured_stdout_lines,
        peak_rss_kib,
    )
    rss_before_load_kib = sample_rss_kib(process.pid)
    if rss_before_load_kib is not None and (peak_rss_kib is None or rss_before_load_kib > peak_rss_kib):
        peak_rss_kib = rss_before_load_kib

    if process.stdin is None:
        raise RuntimeError("process stdin is not available")
    process.stdin.write("load\n")
    process.stdin.flush()

    peak_rss_kib = wait_for_phase(
        process,
        "ready_for_end",
        sample_interval_ms,
        captured_stdout_lines,
        peak_rss_kib,
    )
    rss_after_load_kib = sample_rss_kib(process.pid)
    if rss_after_load_kib is not None and (peak_rss_kib is None or rss_after_load_kib > peak_rss_kib):
        peak_rss_kib = rss_after_load_kib

    process.stdin.write("exit\n")
    process.stdin.flush()

    stdout_tail, stderr_text = process.communicate()
    if process.returncode != 0:
        raise RuntimeError(
            f"run failed (exit={process.returncode}):\n"
            f"stdout:\n{''.join(captured_stdout_lines)}\n{stdout_tail}\n"
            f"stderr:\n{stderr_text}"
        )

    if stdout_tail:
        captured_stdout_lines.extend(stdout_tail.splitlines())

    snapshot_size = extract_snapshot_size("\n".join(captured_stdout_lines), run_dir)
    return (
        rss_before_load_kib,
        rss_after_load_kib,
        peak_rss_kib,
        snapshot_size,
        "\n".join(captured_stdout_lines),
        stderr_text,
    )


def format_ratio(numerator: int, denominator: int) -> str:
    if denominator <= 0:
        return "n/a"
    return f"{numerator / denominator:.3f}"


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[2]
    binary = ensure_binary((repo_root / args.binary) if not args.binary.is_absolute() else args.binary)

    tmp_dir = (repo_root / args.tmp_dir) if not args.tmp_dir.is_absolute() else args.tmp_dir
    if tmp_dir.exists() and not args.keep_artifacts:
        shutil.rmtree(tmp_dir)
    tmp_dir.mkdir(parents=True, exist_ok=True)

    reference_input_file = tmp_dir / "reference_input_empty.jsonl"
    reference_input_file.write_text("# empty input\n", encoding="utf-8")

    if args.input_file is not None:
        workload_input_file = (repo_root / args.input_file) if not args.input_file.is_absolute() else args.input_file
        scenario_name = f"input:{workload_input_file.name}"
    else:
        scenario = SCENARIOS[args.scenario]
        workload_input_file = tmp_dir / f"{scenario.name}.jsonl"
        run_generate_messages(repo_root, workload_input_file, scenario)
        scenario_name = scenario.name

    run_dir = tmp_dir / "run"
    run_dir.mkdir(parents=True, exist_ok=True)

    (
        empty_rss_before_load,
        empty_rss_after_load,
        empty_peak_rss,
        empty_snapshot_bytes,
        _,
        _,
    ) = run_measured(
        binary,
        reference_input_file,
        run_dir,
        args.sample_interval_ms,
    )
    (
        workload_rss_before_load,
        workload_rss_after_load,
        workload_peak_rss,
        workload_snapshot_bytes,
        _,
        _,
    ) = run_measured(
        binary,
        workload_input_file,
        run_dir,
        args.sample_interval_ms,
    )

    delta_peak_rss_kib = (
        workload_peak_rss - empty_peak_rss
        if workload_peak_rss is not None and empty_peak_rss is not None
        else None
    )
    delta_rss_after_load_kib = (
        workload_rss_after_load - empty_rss_after_load
        if workload_rss_after_load is not None and empty_rss_after_load is not None
        else None
    )

    reference_snapshot_kib = bytes_to_kib(empty_snapshot_bytes)
    workload_exported_file_size_kib = bytes_to_kib(workload_snapshot_bytes)

    print(f"scenario={scenario_name}")
    print(f"input_file={workload_input_file}")
    print(f"exported_file_size_kib={workload_exported_file_size_kib}")
    print(f"rss_before_load_kib={workload_rss_before_load}")
    print(f"rss_after_load_kib={workload_rss_after_load}")
    print(f"sampled_rss_peak_kib={workload_peak_rss}")
    print(f"reference_rss_after_load_command_kib={empty_rss_after_load}")
    print(f"ram_growth_kib={delta_rss_after_load_kib}")

    if workload_exported_file_size_kib is not None and workload_exported_file_size_kib > 0 and delta_rss_after_load_kib is not None and delta_rss_after_load_kib > 0:
        print(
            "ratio.ram_growth_to_exported_file="
            f"{format_ratio(delta_rss_after_load_kib, workload_exported_file_size_kib)}"
        )

    if not args.keep_artifacts:
        shutil.rmtree(tmp_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
