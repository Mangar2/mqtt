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


SEND_RATE_PER_SECOND = 2000
SEND_SECONDS = 10.0
EVAL_TOTAL_SECONDS = 20.0
POST_CHECK_SECONDS = 5.0
TARGET_SEND_TOTAL = int(SEND_RATE_PER_SECOND * SEND_SECONDS)


def _load_mqtt_helper():
    spec = importlib.util.spec_from_file_location("qos1_arrival_helper", HELPER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load helper from {HELPER_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_mqtt_helper = _load_mqtt_helper()
MqttClient = _mqtt_helper.MqttClient


def _parse_sequence(payload: bytes, prefix: str) -> int | None:
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


def _drain_counts(subscriber: MqttClient, publisher: MqttClient, prefix: str) -> tuple[int, int, set[int]]:
    acked_delta = 0
    recv_delta_total = 0
    recv_sequences: set[int] = set()

    completed = publisher.drain_published_mids(limit=65536)
    if completed:
        for _mid_value, reason_value in completed.items():
            if int(reason_value) in (0x00, 0x10):
                acked_delta += 1

    drained = subscriber.drain_available_messages(limit=8192)
    if drained:
        recv_delta_total = len(drained)
        for message in drained:
            sequence = _parse_sequence(bytes(message.payload), prefix)
            if sequence is not None:
                recv_sequences.add(sequence)

    return acked_delta, recv_delta_total, recv_sequences


def run(args: argparse.Namespace) -> int:
    try:
        with socket.create_connection((args.host, args.port), timeout=1.0):
            pass
    except OSError as error:
        print(f"RESULT preflight: FAIL target_unreachable={error}")
        return 2

    topic = f"bug/qos1/arrival/{uuid.uuid4().hex[:10]}"
    payload_prefix = "q1"

    print("=== QoS1 Arrival Dedicated Repro ===")
    print(f"target           : {args.host}:{args.port}")
    print(f"topic            : {topic}")
    print(f"send phase       : {SEND_SECONDS:.1f}s @ {SEND_RATE_PER_SECOND}/s")
    print(f"evaluation point : {EVAL_TOTAL_SECONDS:.1f}s total runtime")
    print(f"post-check       : {POST_CHECK_SECONDS:.1f}s no-more-arrivals check")

    attempted = 0
    sent = 0
    send_errors = 0
    acked = 0
    received_total = 0
    received_by_eval = 0

    sent_sequences: set[int] = set()
    received_sequences: set[int] = set()
    received_sequences_by_eval: set[int] = set()

    with MqttClient(client_id=f"arrival-sub-{uuid.uuid4().hex[:6]}", timeout_seconds=20.0) as subscriber:
        subscriber.connect(args.host, args.port, clean_start=True)
        suback = subscriber.subscribe(topic, qos=1)
        if not suback or int(suback[0]) > 2:
            print(f"RESULT preflight: FAIL suback={suback}")
            return 2

        with MqttClient(client_id=f"arrival-pub-{uuid.uuid4().hex[:6]}", timeout_seconds=20.0) as publisher:
            publisher.connect(args.host, args.port, clean_start=True)

            started = time.monotonic()
            send_end = started + SEND_SECONDS
            eval_end = started + EVAL_TOTAL_SECONDS
            post_end = eval_end + POST_CHECK_SECONDS
            sequence = 1

            while time.monotonic() < eval_end:
                now = time.monotonic()

                elapsed_send = min(SEND_SECONDS, max(0.0, now - started))
                should_have_attempted = int(math.floor((elapsed_send * float(SEND_RATE_PER_SECOND)) + 1e-9))
                to_attempt = max(0, should_have_attempted - attempted)

                for _ in range(to_attempt):
                    payload = f"{payload_prefix}:{sequence}".encode("utf-8")
                    attempted += 1
                    try:
                        publisher.publish(topic, payload, qos=1, wait_for_qos1_publish=False)
                        sent += 1
                        sent_sequences.add(sequence)
                    except RuntimeError:
                        send_errors += 1
                    sequence += 1

                ack_delta, recv_delta, recv_sequences = _drain_counts(subscriber, publisher, payload_prefix)
                acked += ack_delta
                received_total += recv_delta
                if recv_sequences:
                    received_sequences.update(recv_sequences)
                    if now < eval_end:
                        received_sequences_by_eval.update(recv_sequences)

                time.sleep(0.001)

            received_by_eval = len(received_sequences_by_eval)

            post_arrivals = 0
            post_arrival_sequences: set[int] = set()
            while time.monotonic() < post_end:
                ack_delta, recv_delta, recv_sequences = _drain_counts(subscriber, publisher, payload_prefix)
                acked += ack_delta
                if recv_delta > 0:
                    post_arrivals += recv_delta
                    received_total += recv_delta
                if recv_sequences:
                    post_arrival_sequences.update(recv_sequences)
                    received_sequences.update(recv_sequences)
                time.sleep(0.001)

    sent_unique = len(sent_sequences)
    received_unique_by_eval = len(received_sequences_by_eval)

    success_all_arrived_in_20s = (sent_unique == TARGET_SEND_TOTAL and received_unique_by_eval == sent_unique)

    print("\n=== Result ===")
    print(
        "SUMMARY counters "
        f"target_sent={TARGET_SEND_TOTAL} attempted={attempted} sent={sent} send_errors={send_errors} "
        f"acked={acked} received_total={received_total} received_by_20s={received_by_eval}"
    )
    print(
        "SUMMARY unique "
        f"sent_unique={sent_unique} received_unique_by_20s={received_unique_by_eval} "
        f"missing_by_20s={max(0, sent_unique - received_unique_by_eval)}"
    )
    print(
        "RESULT arrival_in_20s: "
        f"{'PASS' if success_all_arrived_in_20s else 'FAIL'} "
        f"sent_unique={sent_unique} received_unique_by_20s={received_unique_by_eval} target_sent={TARGET_SEND_TOTAL}"
    )
    print(
        "INFO post_5s_arrivals: "
        f"count={post_arrivals} unique={len(post_arrival_sequences)}"
    )

    return 0 if success_all_arrived_in_20s else 1


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Dedicated QoS1 arrival proof: 1000/s for 10s, evaluate at 20s, then 5s late-arrival check")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    return parser.parse_args()


if __name__ == "__main__":
    sys.exit(run(_parse_args()))
