#!/usr/bin/env python3
from __future__ import annotations

import argparse
import contextlib
import importlib.util
import math
import socket
import sys
import time
import uuid
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[3]
HELPER_PATH = ROOT_DIR / "test" / "integration_tests" / "helpers" / "mqtt_client.py"


DEFAULT_PUBLISHER_COUNT = 1
DEFAULT_SUBSCRIBER_COUNT = 1
DEFAULT_SEND_RATE_PER_PUBLISHER_PER_SECOND = 400
SEND_SECONDS = 10.0
EVAL_TOTAL_SECONDS = 20.0
POST_CHECK_SECONDS = 5.0


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


def _message_id(publisher_index: int, sequence: int) -> int:
    return (int(publisher_index) * 1_000_000) + int(sequence)


def _drain_counts(
    subscribers: list[MqttClient],
    publishers: list[MqttClient],
    prefix: str,
) -> tuple[int, int, set[int]]:
    acked_delta = 0
    recv_delta_total = 0
    recv_sequences: set[int] = set()

    for publisher in publishers:
        completed = publisher.drain_published_mids(limit=65536)
        if completed:
            for _mid_value, reason_value in completed.items():
                if int(reason_value) in (0x00, 0x10):
                    acked_delta += 1

    for subscriber in subscribers:
        drained = subscriber.drain_available_messages(limit=8192)
        if not drained:
            continue
        recv_delta_total += len(drained)
        for message in drained:
            sequence = _parse_sequence(bytes(message.payload), prefix)
            if sequence is None:
                continue
            publisher_index = int(message.topic.rsplit("/", 1)[-1])
            recv_sequences.add(_message_id(publisher_index, sequence))

    return acked_delta, recv_delta_total, recv_sequences


def run(args: argparse.Namespace) -> int:
    publisher_count = max(1, int(args.publishers))
    subscriber_count = max(1, int(args.subscribers))
    send_rate_per_publisher = max(1, int(args.send_rate_per_publisher))
    target_send_total = int(publisher_count * send_rate_per_publisher * SEND_SECONDS)

    if publisher_count != subscriber_count:
        print(
            "RESULT preflight: FAIL "
            f"publisher_count={publisher_count} subscriber_count={subscriber_count} "
            "must_be_equal_for_1_to_1_topic_pairing"
        )
        return 2

    try:
        with socket.create_connection((args.host, args.port), timeout=1.0):
            pass
    except OSError as error:
        print(f"RESULT preflight: FAIL target_unreachable={error}")
        return 2

    topic_root = f"bug/qos1/arrival/{uuid.uuid4().hex[:10]}"
    topics = [f"{topic_root}/{index}" for index in range(publisher_count)]
    payload_prefix = "q1"

    print("=== QoS1 Arrival Dedicated Repro ===")
    print(f"target           : {args.host}:{args.port}")
    print(f"topic root       : {topic_root}")
    print(
        "clients          : "
        f"publishers={publisher_count} subscribers={subscriber_count} "
        f"rate_per_publisher={send_rate_per_publisher}/s"
    )
    print(f"send phase       : {SEND_SECONDS:.1f}s @ {publisher_count * send_rate_per_publisher}/s total")
    print(f"evaluation point : {EVAL_TOTAL_SECONDS:.1f}s total runtime")
    print(f"post-check       : {POST_CHECK_SECONDS:.1f}s no-more-arrivals check")

    attempted = 0
    sent = 0
    send_errors = 0
    acked = 0
    received_total = 0
    received_by_eval = 0

    sent_sequences: set[int] = set()
    received_sequences_by_eval: set[int] = set()

    attempts_by_publisher = [0 for _ in range(publisher_count)]
    next_sequence_by_publisher = [1 for _ in range(publisher_count)]

    with contextlib.ExitStack() as stack:
        subscribers: list[MqttClient] = []
        publishers: list[MqttClient] = []

        for subscriber_index in range(subscriber_count):
            subscriber = stack.enter_context(
                MqttClient(client_id=f"arrival-sub-{subscriber_index}-{uuid.uuid4().hex[:6]}", timeout_seconds=20.0)
            )
            subscriber.connect(args.host, args.port, clean_start=True)
            suback = subscriber.subscribe(topics[subscriber_index], qos=1)
            if not suback or int(suback[0]) > 2:
                print(f"RESULT preflight: FAIL suback={suback} subscriber_index={subscriber_index}")
                return 2
            subscribers.append(subscriber)

        for publisher_index in range(publisher_count):
            publisher = stack.enter_context(
                MqttClient(client_id=f"arrival-pub-{publisher_index}-{uuid.uuid4().hex[:6]}", timeout_seconds=20.0)
            )
            publisher.connect(args.host, args.port, clean_start=True)
            publishers.append(publisher)

        started = time.monotonic()
        eval_end = started + EVAL_TOTAL_SECONDS
        post_end = eval_end + POST_CHECK_SECONDS

        while time.monotonic() < eval_end:
            now = time.monotonic()

            elapsed_send = min(SEND_SECONDS, max(0.0, now - started))

            for publisher_index, publisher in enumerate(publishers):
                should_have_attempted = int(
                    math.floor((elapsed_send * float(send_rate_per_publisher)) + 1e-9)
                )
                to_attempt = max(0, should_have_attempted - attempts_by_publisher[publisher_index])

                for _ in range(to_attempt):
                    sequence = next_sequence_by_publisher[publisher_index]
                    payload = f"{payload_prefix}:{sequence}".encode("utf-8")
                    attempted += 1
                    attempts_by_publisher[publisher_index] += 1
                    try:
                        publisher.publish(
                            topics[publisher_index],
                            payload,
                            qos=1,
                            wait_for_qos1_publish=False,
                        )
                        sent += 1
                        sent_sequences.add(_message_id(publisher_index, sequence))
                    except RuntimeError:
                        send_errors += 1
                    next_sequence_by_publisher[publisher_index] += 1

            ack_delta, recv_delta, recv_sequences = _drain_counts(subscribers, publishers, payload_prefix)
            acked += ack_delta
            received_total += recv_delta
            if recv_sequences and now < eval_end:
                received_sequences_by_eval.update(recv_sequences)

            time.sleep(0.001)

        post_arrivals = 0
        post_arrival_sequences: set[int] = set()
        while time.monotonic() < post_end:
            ack_delta, recv_delta, recv_sequences = _drain_counts(subscribers, publishers, payload_prefix)
            acked += ack_delta
            if recv_delta > 0:
                post_arrivals += recv_delta
                received_total += recv_delta
            if recv_sequences:
                post_arrival_sequences.update(recv_sequences)
            time.sleep(0.001)

    sent_unique = len(sent_sequences)
    received_unique_by_eval = len(received_sequences_by_eval)

    success_all_arrived_in_20s = (sent_unique == target_send_total and received_unique_by_eval == sent_unique)

    print("\n=== Result ===")
    print(
        "SUMMARY counters "
        f"target_sent={target_send_total} attempted={attempted} sent={sent} send_errors={send_errors} "
        f"acked={acked} received_total={received_total} received_by_20s={received_unique_by_eval}"
    )
    print(
        "SUMMARY unique "
        f"sent_unique={sent_unique} received_unique_by_20s={received_unique_by_eval} "
        f"missing_by_20s={max(0, sent_unique - received_unique_by_eval)}"
    )
    print(
        "RESULT arrival_in_20s: "
        f"{'PASS' if success_all_arrived_in_20s else 'FAIL'} "
        f"sent_unique={sent_unique} received_unique_by_20s={received_unique_by_eval} target_sent={target_send_total}"
    )
    print(
        "INFO post_5s_arrivals: "
        f"count={post_arrivals} unique={len(post_arrival_sequences)}"
    )

    return 0 if success_all_arrived_in_20s else 1


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Dedicated QoS1 arrival proof: 5 publishers + 5 subscribers, 400/s each publisher, evaluate at 20s, then 5s post-check"
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--publishers", type=int, default=DEFAULT_PUBLISHER_COUNT)
    parser.add_argument("--subscribers", type=int, default=DEFAULT_SUBSCRIBER_COUNT)
    parser.add_argument("--send-rate-per-publisher", type=int, default=DEFAULT_SEND_RATE_PER_PUBLISHER_PER_SECOND)
    return parser.parse_args()


if __name__ == "__main__":
    sys.exit(run(_parse_args()))
