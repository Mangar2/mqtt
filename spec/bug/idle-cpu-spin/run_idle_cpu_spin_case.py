#!/usr/bin/env python3
from __future__ import annotations

"""Run a short fixed-rate idle-cpu-spin reproduction on Raspberry Pi.

Flow:
1) Build and deploy broker using test/deploypi.py
2) Run a short QoS0 load at fixed 4000 msg/s
3) Save run summary and CPU snapshot files on Raspberry Pi

Only trace startup parameters are configurable.
"""

import argparse
import importlib.util
import json
import shlex
import subprocess
import sys
import time
import uuid
from dataclasses import asdict
from dataclasses import dataclass
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[3]
DEPLOY_SCRIPT = PROJECT_ROOT / "test" / "deploypi.py"
MQTT_HELPER = PROJECT_ROOT / "test" / "integration_tests" / "helpers" / "mqtt_client.py"

REMOTE_HOST = "pi@raspberrypi"
REMOTE_DIR = "/home/pi/mqtt/idle-cpu-spin"
REMOTE_CONFIG = "../broker.ws.ini"
TARGET_HOST = "raspberrypi"
TARGET_PORT = 1883

LOAD_RATE_MESSAGES_PER_SECOND = 4000
LOAD_DURATION_SECONDS = 20.0
LOAD_DRAIN_SECONDS = 2.0

TRACE_LEVEL_CHOICES = ("none", "error", "warning", "info", "trace")


@dataclass(frozen=True)
class RunResult:
    sent: int
    received: int
    requested_rate: int
    achieved_send_rate: float
    achieved_receive_rate: float
    elapsed_seconds: float
    topic: str


def run_or_fail(command: list[str], *, label: str, cwd: Path | None = None) -> None:
    print(f"STEP {label}...")
    completed = subprocess.run(command, cwd=cwd or PROJECT_ROOT, check=False)
    if completed.returncode != 0:
        raise RuntimeError(f"{label} failed with exit code {completed.returncode}")
    print(f"STEP {label} ok")


def run_capture_or_fail(command: list[str], *, label: str) -> str:
    print(f"STEP {label}...")
    completed = subprocess.run(
        command,
        cwd=PROJECT_ROOT,
        check=False,
        text=True,
        capture_output=True,
    )
    if completed.returncode != 0:
        stderr = completed.stderr.strip()
        raise RuntimeError(f"{label} failed with exit code {completed.returncode}: {stderr}")
    print(f"STEP {label} ok")
    return completed.stdout


def load_mqtt_client_class():
    digest = uuid.uuid4().hex[:8]
    spec = importlib.util.spec_from_file_location(f"idle_cpu_spin_mqtt_client_{digest}", MQTT_HELPER)
    if spec is None or spec.loader is None:
        raise RuntimeError("unable to load mqtt client helper")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.MqttClient


def deploy_broker(trace_level: str, trace_modules: list[str]) -> None:
    command = [
        "python3",
        str(DEPLOY_SCRIPT),
        "--remote-host",
        REMOTE_HOST,
        "--remote-dir",
        REMOTE_DIR,
        "--remote-config",
        REMOTE_CONFIG,
    ]
    if trace_level:
        command.extend(["--trace-level", trace_level])
    for module_name in trace_modules:
        command.extend(["--trace-module", module_name])

    run_or_fail(command, label="build+deploy+restart broker")


def run_short_p01_like_load() -> RunResult:
    MqttClient = load_mqtt_client_class()
    topic = f"bug/idle-cpu-spin/{uuid.uuid4().hex[:10]}"

    sent_total = 0
    received_total = 0
    sequence = 1
    start = time.monotonic()
    end = start + LOAD_DURATION_SECONDS

    with MqttClient(timeout_seconds=20.0) as subscriber:
        connack = subscriber.connect(TARGET_HOST, TARGET_PORT, client_id=f"idle-spin-sub-{uuid.uuid4().hex[:8]}")
        if int(getattr(connack, "reason_code", 255)) != 0:
            raise RuntimeError(f"subscriber connect failed reason={int(getattr(connack, 'reason_code', 255))}")
        subscriber.subscribe(topic, qos=0)

        with MqttClient(timeout_seconds=20.0) as publisher:
            connack = publisher.connect(TARGET_HOST, TARGET_PORT, client_id=f"idle-spin-pub-{uuid.uuid4().hex[:8]}")
            if int(getattr(connack, "reason_code", 255)) != 0:
                raise RuntimeError(f"publisher connect failed reason={int(getattr(connack, 'reason_code', 255))}")

            while True:
                now = time.monotonic()
                if now >= end:
                    break

                elapsed = now - start
                requested_total = int(elapsed * float(LOAD_RATE_MESSAGES_PER_SECOND))
                to_send = max(0, requested_total - sent_total)

                for _ in range(to_send):
                    publisher.publish(
                        topic,
                        f"idle-cpu-spin:{sequence}".encode("utf-8"),
                        qos=0,
                        wait_for_qos0_publish=False,
                    )
                    sent_total += 1
                    sequence += 1

                drained = subscriber.drain_available_messages(limit=8192)
                if drained:
                    received_total += len(drained)

                time.sleep(0.001)

            flush_deadline = time.monotonic() + LOAD_DRAIN_SECONDS
            while time.monotonic() < flush_deadline:
                drained = subscriber.drain_available_messages(limit=8192)
                if drained:
                    received_total += len(drained)
                else:
                    time.sleep(0.003)

    elapsed_seconds = max(0.001, time.monotonic() - start)
    return RunResult(
        sent=sent_total,
        received=received_total,
        requested_rate=LOAD_RATE_MESSAGES_PER_SECOND,
        achieved_send_rate=float(sent_total) / elapsed_seconds,
        achieved_receive_rate=float(received_total) / elapsed_seconds,
        elapsed_seconds=elapsed_seconds,
        topic=topic,
    )


def ensure_remote_directory() -> None:
    run_or_fail(
        [
            "ssh",
            "-n",
            "-o",
            "BatchMode=yes",
            REMOTE_HOST,
            f"mkdir -p {shlex.quote(REMOTE_DIR)}",
        ],
        label="prepare remote output directory",
    )


def write_remote_file(remote_path: str, content: str) -> None:
    command = [
        "ssh",
        "-n",
        "-o",
        "BatchMode=yes",
        REMOTE_HOST,
        f"cat > {shlex.quote(remote_path)}",
    ]
    completed = subprocess.run(
        command,
        cwd=PROJECT_ROOT,
        check=False,
        text=True,
        input=content,
        capture_output=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"write remote file failed ({remote_path}): {completed.stderr.strip()}"
        )


def capture_remote_cpu_snapshot(remote_path: str) -> None:
    remote_command = (
        "set -eu; "
        f"SNAP={shlex.quote(remote_path)}; "
        "PID=$(pgrep -n -x mqtt-broker || true); "
        "if [ -z \"$PID\" ]; then "
        "  { date -Is; echo \"mqtt-broker pid not found\"; } > \"$SNAP\"; "
        "else "
        "  { date -Is; echo \"pid=$PID\"; top -H -b -n 1 -p \"$PID\"; } > \"$SNAP\"; "
        "fi"
    )
    run_or_fail(
        ["ssh", "-n", "-o", "BatchMode=yes", REMOTE_HOST, remote_command],
        label="capture remote cpu snapshot",
    )


def read_remote_tail(remote_file: str, lines: int) -> str:
    return run_capture_or_fail(
        [
            "ssh",
            "-n",
            "-o",
            "BatchMode=yes",
            REMOTE_HOST,
            f"tail -n {lines} {shlex.quote(remote_file)} || true",
        ],
        label=f"read remote tail ({Path(remote_file).name})",
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Idle CPU spin optimized testcase runner")
    parser.add_argument(
        "--trace-level",
        choices=TRACE_LEVEL_CHOICES,
        default="none",
        help="Broker trace level passed to deploypi.py",
    )
    parser.add_argument(
        "--trace-module",
        action="append",
        default=[],
        metavar="MODULE",
        help="Repeatable trace module passed to deploypi.py",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    remote_result_path = f"{REMOTE_DIR}/idle-cpu-spin-result.json"
    remote_cpu_path = f"{REMOTE_DIR}/idle-cpu-spin-top-after-load.txt"
    remote_broker_log = f"{REMOTE_DIR}/broker.log"

    try:
        ensure_remote_directory()
        deploy_broker(args.trace_level, list(args.trace_module))

        result = run_short_p01_like_load()
        result_payload = {
            "timestamp_epoch": int(time.time()),
            "remote_host": REMOTE_HOST,
            "remote_dir": REMOTE_DIR,
            "target_host": TARGET_HOST,
            "target_port": TARGET_PORT,
            "trace_level": args.trace_level,
            "trace_modules": list(args.trace_module),
            "load_duration_seconds": LOAD_DURATION_SECONDS,
            "requested_messages_per_second": LOAD_RATE_MESSAGES_PER_SECOND,
            "result": asdict(result),
            "artifacts": {
                "broker_log": remote_broker_log,
                "result_json": remote_result_path,
                "cpu_snapshot": remote_cpu_path,
            },
        }

        write_remote_file(remote_result_path, json.dumps(result_payload, indent=2) + "\n")
        print("STEP waiting 120s for broker to drain (monitoring window)...")
        time.sleep(120.0)
        capture_remote_cpu_snapshot(remote_cpu_path)

        cpu_tail = read_remote_tail(remote_cpu_path, lines=40).strip()
        print("RESULT ok")
        print(
            "RESULT summary "
            f"sent={result.sent} "
            f"recv={result.received} "
            f"requested_rate={result.requested_rate}/s "
            f"send_rate={result.achieved_send_rate:.1f}/s "
            f"recv_rate={result.achieved_receive_rate:.1f}/s "
            f"elapsed={result.elapsed_seconds:.2f}s"
        )
        print(f"RESULT remote_artifact result={remote_result_path}")
        print(f"RESULT remote_artifact cpu={remote_cpu_path}")
        print(f"RESULT remote_artifact broker_log={remote_broker_log}")
        if cpu_tail:
            print("RESULT cpu_tail_start")
            print(cpu_tail)
            print("RESULT cpu_tail_end")

        local_log = PROJECT_ROOT / "test" / "idle-cpu-spin-drain.log"
        run_or_fail(
            ["scp", "-o", "BatchMode=yes", f"{REMOTE_HOST}:{remote_broker_log}", str(local_log)],
            label="download broker log",
        )
        print(f"RESULT local_broker_log={local_log}")
        return 0
    except Exception as error:
        print(f"RESULT fail error={error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
