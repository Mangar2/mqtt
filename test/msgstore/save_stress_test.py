#!/usr/bin/env python3
"""Run MessageStore save stress benchmark in synchronous test mode.

Workflow:
1) generate workload (optional)
2) start yahamsgstoreclient --test --test-handshake
3) load input once
4) trigger repeated 'save' commands
5) report RSS checkpoints and timing
"""

from __future__ import annotations

import argparse
import json
import re
import select
import shutil
import socket
import subprocess
import time
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="MessageStore save stress benchmark")
    parser.add_argument(
        "--binary",
        type=Path,
        default=Path("build/debug/yahamsgstoreclient"),
        help="Path to yahamsgstoreclient binary.",
    )
    parser.add_argument(
        "--input-file",
        type=Path,
        default=None,
        help="Optional existing JSONL input file. If omitted, a dataset is generated.",
    )
    parser.add_argument(
        "--message-count",
        type=int,
        default=1_000_000,
        help="Generated message count (default: 1000000).",
    )
    parser.add_argument(
        "--topic-count",
        type=int,
        default=10_000,
        help="Generated topic count (default: 10000).",
    )
    parser.add_argument(
        "--reason-count",
        type=int,
        default=1,
        help="Reason entries per generated message (default: 1).",
    )
    parser.add_argument(
        "--reason0-step-ms",
        type=int,
        default=1000,
        help="Timestamp step for reason[0] in generated data (default: 1000).",
    )
    parser.add_argument(
        "--reason-text-change-every",
        type=int,
        default=1,
        help="Change reason text cadence in generated data (default: 1).",
    )
    parser.add_argument(
        "--save-count",
        type=int,
        default=1000,
        help="How many save commands to send after load (default: 1000).",
    )
    parser.add_argument(
        "--rss-sample-every",
        type=int,
        default=20,
        help="Sample RSS every N save commands (default: 20).",
    )
    parser.add_argument(
        "--tmp-dir",
        type=Path,
        default=Path("test/msgstore/.tmp/save_stress"),
        help="Temporary benchmark directory.",
    )
    parser.add_argument(
        "--keep-artifacts",
        action="store_true",
        help="Keep generated workload and run dir.",
    )
    return parser.parse_args()


def ensure_binary(path: Path, repo_root: Path) -> Path:
    resolved = (repo_root / path).resolve() if not path.is_absolute() else path.resolve()
    if not resolved.exists():
        raise FileNotFoundError(f"binary not found: {resolved}")
    if not resolved.is_file():
        raise FileNotFoundError(f"binary path is not a file: {resolved}")
    return resolved


def sample_rss_kib(pid: int) -> int | None:
    completed = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)], capture_output=True, text=True)
    if completed.returncode != 0:
        return None
    match = re.search(r"\d+", completed.stdout)
    if match is None:
        return None
    value = int(match.group(0))
    return value if value > 0 else None


def wait_for_phase(process: subprocess.Popen[str], expected_phase: str) -> None:
    if process.stdout is None:
        raise RuntimeError("process stdout is not available")

    token = f"test.handshake phase={expected_phase}"
    while True:
        if process.poll() is not None:
            raise RuntimeError(f"process exited before phase '{expected_phase}'")

        readable, _, _ = select.select([process.stdout], [], [], 0.2)
        if not readable:
            continue

        line = process.stdout.readline()
        if line == "":
            raise RuntimeError(f"stdout closed before phase '{expected_phase}'")
        if token in line:
            return


def wait_for_saved_index(process: subprocess.Popen[str], expected_index: int) -> None:
    if process.stdout is None:
        raise RuntimeError("process stdout is not available")

    token = f"test.handshake phase=saved index={expected_index}"
    while True:
        if process.poll() is not None:
            raise RuntimeError(f"process exited before save index {expected_index}")

        readable, _, _ = select.select([process.stdout], [], [], 0.2)
        if not readable:
            continue

        line = process.stdout.readline()
        if line == "":
            raise RuntimeError(f"stdout closed before save index {expected_index}")
        if token in line:
            return


def generate_input_if_needed(args: argparse.Namespace, repo_root: Path, tmp_dir: Path) -> Path:
    if args.input_file is not None:
        workload = (repo_root / args.input_file).resolve() if not args.input_file.is_absolute() else args.input_file.resolve()
        if not workload.exists():
            raise FileNotFoundError(f"input file not found: {workload}")
        return workload

    generator = (repo_root / "test/msgstore/generate_messages.py").resolve()
    workload = (tmp_dir / "save_stress_1m_10k.jsonl").resolve()
    cmd = [
        "python3",
        str(generator),
        "--message-count",
        str(args.message_count),
        "--topic-count",
        str(args.topic_count),
        "--reason-count",
        str(args.reason_count),
        "--reason0-step-ms",
        str(args.reason0_step_ms),
        "--reason-text-change-every",
        str(args.reason_text_change_every),
        "--output-file",
        str(workload),
        "--seed",
        "42",
    ]
    completed = subprocess.run(cmd, cwd=repo_root, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RuntimeError(
            "generate_messages failed\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )
    return workload


def run_benchmark(binary: Path,
                  input_file: Path,
                  save_count: int,
                  rss_sample_every: int,
                  run_dir: Path) -> dict[str, object]:
    cmd = [str(binary), "--test", str(input_file), "--test-handshake"]
    process = subprocess.Popen(
        cmd,
        cwd=run_dir,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    if process.stdin is None:
        raise RuntimeError("process stdin is not available")

    wait_for_phase(process, "ready_for_start")
    rss_before_load = sample_rss_kib(process.pid)

    load_start = time.perf_counter()
    process.stdin.write("load\n")
    process.stdin.flush()
    wait_for_phase(process, "ready_for_end")
    load_elapsed_s = time.perf_counter() - load_start

    rss_after_load = sample_rss_kib(process.pid)

    save_rss_samples: list[tuple[int, int | None]] = []
    save_start = time.perf_counter()
    for save_index in range(1, save_count + 1):
        process.stdin.write("save\n")
        process.stdin.flush()
        wait_for_saved_index(process, save_index)

        if save_index % rss_sample_every == 0 or save_index == save_count:
            save_rss_samples.append((save_index, sample_rss_kib(process.pid)))

    save_elapsed_s = time.perf_counter() - save_start
    rss_after_saves = sample_rss_kib(process.pid)

    process.stdin.write("exit\n")
    process.stdin.flush()
    stdout_text, stderr_text = process.communicate(timeout=1800)
    if process.returncode != 0:
        raise RuntimeError(
            f"benchmark process failed (exit={process.returncode})\n"
            f"stdout:\n{stdout_text}\n"
            f"stderr:\n{stderr_text}"
        )

    return {
        "rss_before_load_kib": rss_before_load,
        "rss_after_load_kib": rss_after_load,
        "rss_after_saves_kib": rss_after_saves,
        "load_elapsed_s": round(load_elapsed_s, 3),
        "save_elapsed_s": round(save_elapsed_s, 3),
        "save_rss_samples": save_rss_samples,
    }


def main() -> int:
    args = parse_args()
    if args.save_count <= 0:
        raise ValueError("--save-count must be > 0")
    if args.rss_sample_every <= 0:
        raise ValueError("--rss-sample-every must be > 0")

    repo_root = Path(__file__).resolve().parents[2]
    tmp_dir = (repo_root / args.tmp_dir).resolve() if not args.tmp_dir.is_absolute() else args.tmp_dir.resolve()
    run_dir = (tmp_dir / "run").resolve()

    if tmp_dir.exists() and not args.keep_artifacts:
        shutil.rmtree(tmp_dir)
    run_dir.mkdir(parents=True, exist_ok=True)

    binary = ensure_binary(args.binary, repo_root)
    input_file = generate_input_if_needed(args, repo_root, tmp_dir)

    result = run_benchmark(
        binary=binary,
        input_file=input_file,
        save_count=args.save_count,
        rss_sample_every=args.rss_sample_every,
        run_dir=run_dir,
    )

    print(f"input_file={input_file}")
    print(f"save_count={args.save_count}")
    print(f"rss_before_load_kib={result['rss_before_load_kib']}")
    print(f"rss_after_load_kib={result['rss_after_load_kib']}")
    print(f"rss_after_saves_kib={result['rss_after_saves_kib']}")
    print(f"load_elapsed_s={result['load_elapsed_s']}")
    print(f"save_elapsed_s={result['save_elapsed_s']}")
    print("rss_samples_kib_by_save_index=")
    for save_index, rss_kib in result["save_rss_samples"]:
        print(f"  {save_index}:{rss_kib}")

    if not args.keep_artifacts:
        shutil.rmtree(tmp_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
