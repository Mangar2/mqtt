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

TOTAL_MESSAGES = 20000
SEND_SECONDS = 10.0
TOTAL_WAIT_SECONDS = 20.0
MAX_RATE_PER_PUBLISHER = 2000.0
MAX_INFLIGHT = 50


def _load_mqtt_helper():
    spec = importlib.util.spec_from_file_location("qos1_cpu_spike_helper", HELPER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load helper from {HELPER_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_mqtt_helper = _load_mqtt_helper()
MqttClient = _mqtt_helper.MqttClient


def _message_id(publisher_index: int, sequence: int) -> int:
    return (publisher_index * 1_000_000) + sequence


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


def run(args: argparse.Namespace) -> int:
    try:
        with socket.create_connection((args.host, args.port), timeout=1.2):
            pass
    except OSError as error:
        print(f"RESULT preflight: FAIL target_unreachable={error}")
        return 2

    total_rate = float(TOTAL_MESSAGES) / SEND_SECONDS
    publisher_count = max(1, int(math.ceil(total_rate / MAX_RATE_PER_PUBLISHER)))
    rate_per_publisher = total_rate / float(publisher_count)

    topic_root = f"bug/qos1/cpu-spike/{uuid.uuid4().hex[:10]}"
    topics = [f"{topic_root}/{index}" for index in range(publisher_count)]
    payload_prefix = "q1"

    print("=== QoS1 CPU Spike Repro ===")
    print(f"target               : {args.host}:{args.port}")
    print(f"topic_root           : {topic_root}")
    print(f"total_messages       : {TOTAL_MESSAGES}")
    print(f"send_seconds         : {SEND_SECONDS:.1f}")
    print(f"total_wait_seconds   : {TOTAL_WAIT_SECONDS:.1f}")
    print(f"total_rate           : {total_rate:.1f}/s")
    print(f"publishers           : {publisher_count}")
    print(f"rate_per_publisher   : {rate_per_publisher:.1f}/s")
    print(f"max_inflight_client  : {MAX_INFLIGHT}")

    attempted = 0
    sent = 0
    acked = 0
    received = 0
    send_errors = 0

    sent_ids: set[int] = set()
    received_ids_by_20s: set[int] = set()

    next_sequence_by_publisher = [1 for _ in range(publisher_count)]

    subscriber = MqttClient(client_id=f"cpu-spike-sub-{uuid.uuid4().hex[:6]}", timeout_seconds=20.0)
    subscriber.connect(args.host, args.port, clean_start=True)
    for topic in topics:
        suback = subscriber.subscribe(topic, qos=1)
        if not suback or int(suback[0]) > 2:
            print(f"RESULT preflight: FAIL suback={suback} topic={topic}")
            return 2

    publishers: list[MqttClient] = []
    for index in range(publisher_count):
        publisher = MqttClient(
            client_id=f"cpu-spike-pub-{index}-{uuid.uuid4().hex[:6]}",
            timeout_seconds=20.0,
            max_inflight_messages=MAX_INFLIGHT,
        )
        publisher.connect(args.host, args.port, clean_start=True)
        publishers.append(publisher)

    started = time.monotonic()
    send_end = started + SEND_SECONDS
    eval_end = started + TOTAL_WAIT_SECONDS

    attempts_by_publisher = [0 for _ in range(publisher_count)]
    target_per_publisher = [
        int(round(((index + 1) * TOTAL_MESSAGES) / float(publisher_count)))
        - int(round((index * TOTAL_MESSAGES) / float(publisher_count)))
        for index in range(publisher_count)
    ]
    send_phase_finalized = False

    while time.monotonic() < eval_end:
        now = time.monotonic()

        if now < send_end:
            elapsed = max(0.0, now - started)
            for publisher_index, publisher in enumerate(publishers):
                per_publisher_target = target_per_publisher[publisher_index]
                if attempts_by_publisher[publisher_index] >= per_publisher_target:
                    continue

                should_have_sent = int(
                    math.floor((elapsed * rate_per_publisher) + 1e-9)
                )
                should_have_sent = min(should_have_sent, per_publisher_target)
                to_send = max(0, should_have_sent - attempts_by_publisher[publisher_index])

                for _ in range(to_send):
                    sequence = next_sequence_by_publisher[publisher_index]
                    payload = f"{payload_prefix}:{sequence}".encode("utf-8")
                    attempted += 1
                    attempts_by_publisher[publisher_index] += 1
                    try:
                        publisher.publish(topic=topics[publisher_index], payload=payload, qos=1, wait_for_qos1_publish=False)
                        sent += 1
                        sent_ids.add(_message_id(publisher_index, sequence))
                    except RuntimeError:
                        send_errors += 1
                    next_sequence_by_publisher[publisher_index] += 1
        elif not send_phase_finalized:
            for publisher_index, publisher in enumerate(publishers):
                per_publisher_target = target_per_publisher[publisher_index]
                while attempts_by_publisher[publisher_index] < per_publisher_target:
                    sequence = next_sequence_by_publisher[publisher_index]
                    payload = f"{payload_prefix}:{sequence}".encode("utf-8")
                    attempted += 1
                    attempts_by_publisher[publisher_index] += 1
                    try:
                        publisher.publish(topic=topics[publisher_index], payload=payload, qos=1, wait_for_qos1_publish=False)
                        sent += 1
                        sent_ids.add(_message_id(publisher_index, sequence))
                    except RuntimeError:
                        send_errors += 1
                    next_sequence_by_publisher[publisher_index] += 1
            send_phase_finalized = True

        for publisher in publishers:
            completed = publisher.drain_published_mids(limit=65536)
            if completed:
                for _mid, reason in completed.items():
                    if int(reason) in (0x00, 0x10):
                        acked += 1

        drained = subscriber.drain_available_messages(limit=16384)
        if drained:
            received += len(drained)
            if now <= eval_end:
                for message in drained:
                    topic_suffix = int(str(message.topic).rsplit("/", 1)[-1])
                    sequence = _parse_payload_sequence(bytes(message.payload), payload_prefix)
                    if sequence is not None:
                        received_ids_by_20s.add(_message_id(topic_suffix, sequence))

        time.sleep(0.001)

    sent_unique = len(sent_ids)
    received_unique_by_20s = len(received_ids_by_20s)
    missing_by_20s = max(0, sent_unique - received_unique_by_20s)

    success = sent_unique == TOTAL_MESSAGES and received_unique_by_20s == sent_unique

    print("\n=== Result ===")
    print(
        "SUMMARY counters "
        f"attempted={attempted} sent={sent} send_errors={send_errors} "
        f"acked={acked} received={received}"
    )
    print(
        "SUMMARY unique "
        f"sent_unique={sent_unique} received_unique_by_20s={received_unique_by_20s} "
        f"missing_by_20s={missing_by_20s}"
    )
    print(
        "RESULT arrival_in_20s: "
        f"{'PASS' if success else 'FAIL'} sent_unique={sent_unique} "
        f"received_unique_by_20s={received_unique_by_20s} target={TOTAL_MESSAGES}"
    )

    for publisher in publishers:
        try:
            publisher.disconnect()
        except Exception:
            pass
    try:
        subscriber.disconnect()
    except Exception:
        pass

    return 0 if success else 1


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Repro for CPU spike with QoS1 high-rate sends (20000 messages in 10s, evaluate at 20s)"
    )
    parser.add_argument("--host", default="qapla")
    parser.add_argument("--port", type=int, default=1883)
    return parser.parse_args()


if __name__ == "__main__":
    sys.exit(run(_parse_args()))
