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
    spec = importlib.util.spec_from_file_location("qos1_loss_helper", HELPER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load helper from {HELPER_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_mqtt_helper = _load_mqtt_helper()
MqttClient = _mqtt_helper.MqttClient


def _safe_rate(count: int, seconds: float) -> float:
    return float(count) / max(1e-9, float(seconds))


def _parse_payload_sequence(payload: bytes, prefix: str) -> int | None:
    try:
        text = payload.decode("utf-8", errors="ignore")
    except Exception:
        return None
    if not text.startswith(prefix):
        return None
    parts = text.split(":", 1)
    if len(parts) != 2:
        return None
    try:
        return int(parts[1])
    except ValueError:
        return None


def _try_publish_qos1(publisher: MqttClient, topic: str, payload: bytes) -> bool:
    try:
        publisher.publish(topic, payload, qos=1, wait_for_qos1_publish=False)
        return True
    except RuntimeError as error:
        if "rc=15" in str(error):
            return False
        raise


def _drain(subscriber: MqttClient, publisher: MqttClient, prefix: str) -> tuple[int, int, set[int], set[int]]:
    delivered_sequences: set[int] = set()
    acked_mids_count = 0
    total_drained_messages = 0

    completed = publisher.drain_published_mids(limit=65536)
    if completed:
        for _mid_value, reason_value in completed.items():
            if int(reason_value) in (0x00, 0x10):
                acked_mids_count += 1

    drained_messages = subscriber.drain_available_messages(limit=8192)
    if drained_messages:
        total_drained_messages = len(drained_messages)
        for message in drained_messages:
            sequence = _parse_payload_sequence(bytes(message.payload), prefix)
            if sequence is not None:
                delivered_sequences.add(sequence)

    return acked_mids_count, total_drained_messages, delivered_sequences, set()


def run(args: argparse.Namespace) -> int:
    try:
        with socket.create_connection((args.host, args.port), timeout=1.0):
            pass
    except OSError as error:
        print(f"RESULT preflight: FAIL target_unreachable={error}")
        return 2

    topic = f"bug/qos1/loss/{uuid.uuid4().hex[:10]}"
    payload_prefix = "q1"
    target_rate = max(1, int(args.target_rate))
    active_seconds = max(1.0, float(args.active_seconds))
    drain_seconds = max(1.0, float(args.drain_seconds))
    min_throughput = float(args.min_recv_rate)
    stall_window_seconds = max(1.0, float(args.stall_window_seconds))

    print("=== QoS1 Repro (throughput + loss) ===")
    print(f"target           : {args.host}:{args.port}")
    print(f"topic            : {topic}")
    print(f"active phase     : {active_seconds:.1f}s @ {target_rate}/s")
    print(f"drain phase      : {drain_seconds:.1f}s")
    print(f"stall window     : {stall_window_seconds:.1f}s")
    print(f"throughput limit : recv_rate_active >= {min_throughput:.2f}/s")

    attempted = 0
    accepted = 0
    queue_full_rejects = 0
    acked = 0
    received = 0
    next_sequence = 1
    sent_sequences: set[int] = set()
    received_sequences: set[int] = set()

    with MqttClient(client_id=f"loss-sub-{uuid.uuid4().hex[:6]}", timeout_seconds=20.0) as subscriber:
        subscriber.connect(args.host, args.port, clean_start=True)
        suback = subscriber.subscribe(topic, qos=1)
        if not suback or int(suback[0]) > 2:
            print(f"RESULT preflight: FAIL suback={suback}")
            return 2

        with MqttClient(client_id=f"loss-pub-{uuid.uuid4().hex[:6]}", timeout_seconds=20.0) as publisher:
            publisher.connect(args.host, args.port, clean_start=True)

            active_started = time.monotonic()
            active_end = active_started + active_seconds

            while time.monotonic() < active_end:
                now = time.monotonic()
                elapsed = now - active_started
                should_have_sent = int(math.floor((elapsed * float(target_rate)) + 1e-9))
                to_send = max(0, should_have_sent - attempted)

                for _ in range(to_send):
                    sequence = next_sequence
                    payload = f"{payload_prefix}:{sequence}".encode("utf-8")
                    if _try_publish_qos1(publisher, topic, payload):
                        accepted += 1
                        sent_sequences.add(sequence)
                    else:
                        queue_full_rejects += 1
                    attempted += 1
                    next_sequence += 1

                ack_delta, recv_delta, delivered_sequences, _ = _drain(subscriber, publisher, payload_prefix)
                acked += ack_delta
                received += recv_delta
                if delivered_sequences:
                    received_sequences.update(delivered_sequences)

                time.sleep(0.001)

            active_elapsed = max(1e-9, time.monotonic() - active_started)
            active_recv_rate = _safe_rate(received, active_elapsed)

            drain_started = time.monotonic()
            drain_end = drain_started + drain_seconds
            last_progress_at = time.monotonic()
            last_acked = acked
            last_received = received

            while time.monotonic() < drain_end:
                ack_delta, recv_delta, delivered_sequences, _ = _drain(subscriber, publisher, payload_prefix)
                if ack_delta > 0 or recv_delta > 0:
                    acked += ack_delta
                    received += recv_delta
                    if delivered_sequences:
                        received_sequences.update(delivered_sequences)
                    last_progress_at = time.monotonic()
                    last_acked = acked
                    last_received = received

                no_progress_for = time.monotonic() - last_progress_at
                if no_progress_for >= stall_window_seconds:
                    break

                time.sleep(0.002)

            drain_elapsed = max(1e-9, time.monotonic() - drain_started)

    sent = accepted
    missing_delivery_ids = sent_sequences - received_sequences
    missing_delivery = len(missing_delivery_ids)
    unacked = max(0, sent - acked)

    throughput_pass = active_recv_rate >= min_throughput

    no_progress_final = (acked == last_acked and received == last_received)
    loss_pass = (missing_delivery == 0 and unacked == 0)

    if not loss_pass and not no_progress_final:
        # Conservative fallback: if counters still move, classify as not proven yet.
        # Keep failure semantics strict for this test by requiring full completion.
        loss_pass = False

    throughput_status = "PASS" if throughput_pass else "FAIL"
    loss_status = "PASS" if loss_pass else "FAIL"

    print("\n=== Result ===")
    print(
        "SUMMARY counters "
        f"attempted={attempted} accepted={accepted} queue_full_rejects={queue_full_rejects} "
        f"acked={acked} received={received} unacked={unacked} missing_delivery={missing_delivery}"
    )
    print(
        "SUMMARY rates "
        f"active_recv_rate={active_recv_rate:.2f}/s target_rate={target_rate}/s min_required={min_throughput:.2f}/s "
        f"active_elapsed={active_elapsed:.3f}s drain_elapsed={drain_elapsed:.3f}s"
    )
    print(
        "RESULT throughput: "
        f"{throughput_status} actual={active_recv_rate:.2f}/s required_min={min_throughput:.2f}/s"
    )
    print(
        "RESULT loss: "
        f"{loss_status} missing_delivery={missing_delivery} unacked={unacked}"
    )

    return 0 if throughput_pass and loss_pass else 1


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Reproduce QoS1 throughput and loss symptoms with explicit PASS/FAIL output")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--target-rate", type=int, default=4000)
    parser.add_argument("--active-seconds", type=float, default=30.0)
    parser.add_argument("--drain-seconds", type=float, default=90.0)
    parser.add_argument("--stall-window-seconds", type=float, default=15.0)
    parser.add_argument("--min-recv-rate", type=float, default=4000.0)
    return parser.parse_args()


if __name__ == "__main__":
    sys.exit(run(_parse_args()))
