#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import math
import socket
import sys
import time
import uuid
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[3]
HELPER_PATH = ROOT_DIR / "test" / "integration_tests" / "helpers" / "mqtt_client.py"


def _load_mqtt_helper():
    spec = importlib.util.spec_from_file_location("qos1_tp_helper", HELPER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load helper from {HELPER_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_mqtt_helper = _load_mqtt_helper()
MqttClient = _mqtt_helper.MqttClient


def _safe_rate(count: int, seconds: float) -> float:
    return float(count) / max(1e-9, float(seconds))


def _try_publish_qos1(publisher: MqttClient, topic: str, payload: bytes) -> bool:
    try:
        publisher.publish(topic, payload, qos=1, wait_for_qos1_publish=False)
        return True
    except RuntimeError as error:
        if "rc=15" in str(error):
            return False
        raise


def run(args: argparse.Namespace) -> int:
    try:
        with socket.create_connection((args.host, args.port), timeout=1.0):
            pass
    except OSError as error:
        print(f"RESULT preflight: FAIL target_unreachable={error}")
        return 2

    topic = f"bug/qos1/throughput/{uuid.uuid4().hex[:10]}"
    target_rate = max(1, int(args.target_rate))
    active_seconds = max(1.0, float(args.active_seconds))
    min_recv_rate = float(args.min_recv_rate)

    print("=== QoS1 Throughput Repro ===")
    print(f"target           : {args.host}:{args.port}")
    print(f"topic            : {topic}")
    print(f"active phase     : {active_seconds:.1f}s @ {target_rate}/s")
    print(f"throughput limit : recv_rate_active >= {min_recv_rate:.2f}/s")

    attempted = 0
    accepted = 0
    queue_full_rejects = 0
    received = 0
    sequence = 1

    with MqttClient(client_id=f"tp-sub-{uuid.uuid4().hex[:6]}", timeout_seconds=20.0) as subscriber:
        subscriber.connect(args.host, args.port, clean_start=True)
        suback = subscriber.subscribe(topic, qos=1)
        if not suback or int(suback[0]) > 2:
            print(f"RESULT preflight: FAIL suback={suback}")
            return 2

        with MqttClient(client_id=f"tp-pub-{uuid.uuid4().hex[:6]}", timeout_seconds=20.0) as publisher:
            publisher.connect(args.host, args.port, clean_start=True)

            active_started = time.monotonic()
            active_end = active_started + active_seconds

            while time.monotonic() < active_end:
                now = time.monotonic()
                elapsed = now - active_started
                should_have_sent = int(math.floor((elapsed * float(target_rate)) + 1e-9))
                to_send = max(0, should_have_sent - attempted)

                for _ in range(to_send):
                    payload = f"t:{sequence}".encode("utf-8")
                    if _try_publish_qos1(publisher, topic, payload):
                        accepted += 1
                    else:
                        queue_full_rejects += 1
                    sequence += 1
                    attempted += 1

                completed = publisher.drain_published_mids(limit=65536)
                if completed:
                    # Completion not used for throughput criterion, but drain callback maps to avoid growth.
                    pass

                drained = subscriber.drain_available_messages(limit=8192)
                if drained:
                    received += len(drained)

                time.sleep(0.001)

            active_elapsed = max(1e-9, time.monotonic() - active_started)

    recv_rate = _safe_rate(received, active_elapsed)
    status = "PASS" if recv_rate >= min_recv_rate else "FAIL"

    print("\n=== Result ===")
    print(
        "SUMMARY counters "
        f"attempted={attempted} accepted={accepted} queue_full_rejects={queue_full_rejects} "
        f"received={received} active_elapsed={active_elapsed:.3f}s"
    )
    print(
        "RESULT throughput: "
        f"{status} actual={recv_rate:.2f}/s required_min={min_recv_rate:.2f}/s"
    )

    return 0 if status == "PASS" else 1


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Reproduce QoS1 low-throughput symptom with explicit PASS/FAIL output")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--target-rate", type=int, default=4000)
    parser.add_argument("--active-seconds", type=float, default=30.0)
    parser.add_argument("--min-recv-rate", type=float, default=4000.0)
    return parser.parse_args()


if __name__ == "__main__":
    sys.exit(run(_parse_args()))
