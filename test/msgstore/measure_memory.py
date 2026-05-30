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
import json
import os
import re
import shutil
import socket
import subprocess
import time
import select
import urllib.parse
import urllib.request
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
    parser.add_argument(
        "--http-request-count",
        type=int,
        default=0,
        help="Optional number of HTTP GET requests after load (default: 0).",
    )
    parser.add_argument(
        "--http-topic",
        type=str,
        default=None,
        help="Topic for HTTP requests. Defaults to first message topic from workload input.",
    )
    parser.add_argument(
        "--http-timeout-ms",
        type=int,
        default=2000,
        help="Per-request HTTP timeout in milliseconds (default: 2000).",
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


def find_free_tcp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        return int(server.getsockname()[1])


def read_first_topic_from_jsonl(input_file: Path) -> str:
    with input_file.open("r", encoding="utf-8") as stream:
        for raw_line in stream:
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            payload = json.loads(line)
            message = payload.get("message")
            if not isinstance(message, dict):
                continue
            topic = message.get("topic")
            if isinstance(topic, str) and topic:
                return topic
    raise RuntimeError(f"could not determine first topic from input file: {input_file}")


def run_http_query_burst(
    port: int,
    topic: str,
    request_count: int,
    timeout_ms: int,
    process_pid: int,
    sample_interval_ms: int,
    current_peak_rss_kib: int | None,
) -> tuple[int | None, int, int, int]:
    if request_count <= 0:
        return current_peak_rss_kib, 0, 0, 0

    encoded_topic = urllib.parse.quote(topic, safe="")
    url = f"http://127.0.0.1:{port}/store/{encoded_topic}"
    timeout_s = max(1, timeout_ms) / 1000.0
    request = urllib.request.Request(
        url,
        headers={
            "history": "true",
            "reason": "true",
            "time": "true",
        },
        method="GET",
    )

    success_count = 0
    fail_count = 0
    response_bytes = 0
    sample_every = max(1, 1000 // max(1, sample_interval_ms))
    for index in range(request_count):
        try:
            with urllib.request.urlopen(request, timeout=timeout_s) as response:
                body = response.read()
                response_bytes += len(body)
                if response.status == 200:
                    success_count += 1
                else:
                    fail_count += 1
        except Exception:
            fail_count += 1

        if index % sample_every == 0:
            rss_kib = sample_rss_kib(process_pid)
            if rss_kib is not None and (current_peak_rss_kib is None or rss_kib > current_peak_rss_kib):
                current_peak_rss_kib = rss_kib

    rss_kib = sample_rss_kib(process_pid)
    if rss_kib is not None and (current_peak_rss_kib is None or rss_kib > current_peak_rss_kib):
        current_peak_rss_kib = rss_kib

    return current_peak_rss_kib, success_count, fail_count, response_bytes
    text = completed.stdout
    if not text:
        return None
    match = re.search(r"\d+", text)
    if match is None:
        return None
    value = int(match.group(0))
    return value if value > 0 else None


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
    http_request_count: int,
    http_topic: str,
    http_timeout_ms: int,
) -> tuple[int | None, int | None, int | None, int | None, int | None, int, int, int, str, str]:
    http_port = find_free_tcp_port() if http_request_count > 0 else None
    cmd = [str(binary), "--test", str(input_file), "--test-handshake"]
    if http_port is not None:
        cmd += ["--test-http-port", str(http_port)]
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

    http_success_count = 0
    http_fail_count = 0
    http_response_bytes = 0
    rss_after_http_kib = rss_after_load_kib
    if http_port is not None and http_request_count > 0:
        (
            peak_rss_kib,
            http_success_count,
            http_fail_count,
            http_response_bytes,
        ) = run_http_query_burst(
            port=http_port,
            topic=http_topic,
            request_count=http_request_count,
            timeout_ms=http_timeout_ms,
            process_pid=process.pid,
            sample_interval_ms=sample_interval_ms,
            current_peak_rss_kib=peak_rss_kib,
        )
        rss_after_http_kib = sample_rss_kib(process.pid)
        if rss_after_http_kib is not None and (peak_rss_kib is None or rss_after_http_kib > peak_rss_kib):
            peak_rss_kib = rss_after_http_kib

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
        rss_after_http_kib,
        peak_rss_kib,
        snapshot_size,
        http_success_count,
        http_fail_count,
        http_response_bytes,
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

    http_topic = args.http_topic if args.http_topic is not None else read_first_topic_from_jsonl(workload_input_file)

    run_dir = tmp_dir / "run"
    run_dir.mkdir(parents=True, exist_ok=True)

    (
        empty_rss_before_load,
        empty_rss_after_load,
        empty_rss_after_http,
        empty_peak_rss,
        empty_snapshot_bytes,
        empty_http_success,
        empty_http_fail,
        empty_http_response_bytes,
        _,
        _,
    ) = run_measured(
        binary,
        reference_input_file,
        run_dir,
        args.sample_interval_ms,
        args.http_request_count,
        http_topic,
        args.http_timeout_ms,
    )
    (
        workload_rss_before_load,
        workload_rss_after_load,
        workload_rss_after_http,
        workload_peak_rss,
        workload_snapshot_bytes,
        workload_http_success,
        workload_http_fail,
        workload_http_response_bytes,
        _,
        _,
    ) = run_measured(
        binary,
        workload_input_file,
        run_dir,
        args.sample_interval_ms,
        args.http_request_count,
        http_topic,
        args.http_timeout_ms,
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
    delta_rss_after_http_kib = (
        workload_rss_after_http - empty_rss_after_http
        if workload_rss_after_http is not None and empty_rss_after_http is not None
        else None
    )

    reference_snapshot_kib = bytes_to_kib(empty_snapshot_bytes)
    workload_exported_file_size_kib = bytes_to_kib(workload_snapshot_bytes)

    print(f"scenario={scenario_name}")
    print(f"input_file={workload_input_file}")
    print(f"http.topic={http_topic}")
    print(f"http.request_count={args.http_request_count}")
    print(f"http.timeout_ms={args.http_timeout_ms}")
    print(f"http.success_count={workload_http_success}")
    print(f"http.fail_count={workload_http_fail}")
    print(f"http.response_bytes={workload_http_response_bytes}")
    print(f"reference.http.success_count={empty_http_success}")
    print(f"reference.http.fail_count={empty_http_fail}")
    print(f"reference.http.response_bytes={empty_http_response_bytes}")
    print(f"exported_file_size_kib={workload_exported_file_size_kib}")
    print(f"rss_before_load_kib={workload_rss_before_load}")
    print(f"rss_after_load_kib={workload_rss_after_load}")
    print(f"rss_after_http_kib={workload_rss_after_http}")
    print(f"sampled_rss_peak_kib={workload_peak_rss}")
    print(f"reference_rss_after_load_command_kib={empty_rss_after_load}")
    print(f"reference_rss_after_http_command_kib={empty_rss_after_http}")
    print(f"ram_growth_kib={delta_rss_after_load_kib}")
    print(f"ram_growth_after_http_kib={delta_rss_after_http_kib}")

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
