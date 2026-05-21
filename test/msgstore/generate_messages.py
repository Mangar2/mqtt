#!/usr/bin/env python3
"""Generate MessageStore benchmark input messages as JSONL."""

from __future__ import annotations

import argparse
import json
import random
import string
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


DEFAULT_TOPIC = "ground/wardrobe/zwave/sys/ventilation/Electric - W"
DEFAULT_QOS = 1
DEFAULT_RETAIN = False
DEFAULT_DUP = False
REASON_TEXT_LENGTH = 100
REASON_PREFIX = "received from zwave network id: "


@dataclass(frozen=True)
class GeneratorConfig:
    message_count: int
    topic_count: int
    reason_count: int
    reason0_step_ms: int
    reason_text_change_every: int | None
    output_file: Path
    fixed_value: float | None
    start_timestamp_ms: int
    seed: int | None


def parse_args() -> GeneratorConfig:
    parser = argparse.ArgumentParser(
        description=(
            "Generate benchmark messages for synchronous MessageStore load tests."
        )
    )
    parser.add_argument(
        "--message-count",
        type=int,
        required=True,
        help="Number of messages to generate.",
    )
    parser.add_argument(
        "--topic-count",
        type=int,
        default=1,
        help="Number of distinct topics to generate (default: 1).",
    )
    parser.add_argument(
        "--reason-count",
        type=int,
        required=True,
        help="Number of reason entries per message.",
    )
    parser.add_argument(
        "--reason0-step-ms",
        type=int,
        required=True,
        help="Timestamp increment in ms for reason index 0 between messages.",
    )
    parser.add_argument(
        "--reason-text-change-every",
        type=int,
        default=None,
        help="Optional cadence for reason text changes; omitted means never change.",
    )
    parser.add_argument(
        "--output-file",
        type=Path,
        required=True,
        help="Output file path.",
    )
    parser.add_argument(
        "--fixed-value",
        type=float,
        default=None,
        help="Optional fixed numeric value used for every generated message.",
    )
    parser.add_argument(
        "--start-timestamp-ms",
        type=int,
        default=int(time.time() * 1000),
        help="Start timestamp in Unix epoch milliseconds (default: now).",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=None,
        help="Optional random seed for reproducible reason text changes.",
    )

    parsed = parser.parse_args()

    if parsed.message_count <= 0:
        raise ValueError("--message-count must be > 0")
    if parsed.topic_count <= 0:
        raise ValueError("--topic-count must be > 0")
    if parsed.reason_count <= 0:
        raise ValueError("--reason-count must be > 0")
    if parsed.reason0_step_ms < 0:
        raise ValueError("--reason0-step-ms must be >= 0")
    if parsed.reason_text_change_every is not None and parsed.reason_text_change_every <= 0:
        raise ValueError("--reason-text-change-every must be > 0")

    return GeneratorConfig(
        message_count=parsed.message_count,
        topic_count=parsed.topic_count,
        reason_count=parsed.reason_count,
        reason0_step_ms=parsed.reason0_step_ms,
        reason_text_change_every=parsed.reason_text_change_every,
        output_file=parsed.output_file,
        fixed_value=parsed.fixed_value,
        start_timestamp_ms=parsed.start_timestamp_ms,
        seed=parsed.seed,
    )


def to_iso_utc(timestamp_ms: int) -> str:
    dt_value = datetime.fromtimestamp(timestamp_ms / 1000.0, tz=timezone.utc)
    return dt_value.strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"


def build_reason_text(random_gen: random.Random) -> str:
    if len(REASON_PREFIX) > REASON_TEXT_LENGTH:
        return REASON_PREFIX[:REASON_TEXT_LENGTH]

    alphabet = string.ascii_letters + string.digits
    suffix_len = REASON_TEXT_LENGTH - len(REASON_PREFIX)
    suffix = "".join(random_gen.choice(alphabet) for _ in range(suffix_len))
    return REASON_PREFIX + suffix


def build_topic_pool(random_gen: random.Random, topic_count: int) -> list[str]:
    if topic_count == 1:
        return [DEFAULT_TOPIC]

    base_segments = DEFAULT_TOPIC.split("/")
    segment_count = len(base_segments)
    topics: list[str] = []

    for topic_index in range(topic_count):
        segment_index = topic_index % segment_count
        variant_segments = list(base_segments)
        token = f"t{topic_index:04d}_{random_gen.randint(1000, 9999)}"
        variant_segments[segment_index] = f"{variant_segments[segment_index]}-{token}"
        topics.append("/".join(variant_segments))

    return topics


def build_message(
    topic: str,
    reason_text: str,
    base_timestamp_ms: int,
    reason_count: int,
    random_gen: random.Random,
    fixed_value: float | None,
) -> dict:
    reasons = []
    for reason_index in range(reason_count):
        reason_timestamp_ms = base_timestamp_ms + reason_index
        reasons.append(
            {
                "message": reason_text,
                "timestamp": to_iso_utc(reason_timestamp_ms),
            }
        )

    if fixed_value is None:
        value = round(random_gen.uniform(0.0, 5000.0), 6)
    else:
        value = fixed_value

    return {
        "topic": topic,
        "value": value,
        "qos": DEFAULT_QOS,
        "retain": DEFAULT_RETAIN,
        "dup": DEFAULT_DUP,
        "reason": reasons,
    }


def build_envelope(message: dict) -> dict:
    return {
        "message": {
            "topic": message["topic"],
            "value": message["value"],
            "reason": list(reversed(message["reason"])),
        }
    }


def generate_messages(config: GeneratorConfig) -> None:
    random_gen = random.Random(config.seed)
    config.output_file.parent.mkdir(parents=True, exist_ok=True)
    topic_pool = build_topic_pool(random_gen, config.topic_count)

    current_reason_text = build_reason_text(random_gen)

    with config.output_file.open("w", encoding="utf-8") as output_stream:
        for message_index in range(config.message_count):
            if (config.reason_text_change_every is not None
                    and message_index > 0
                    and message_index % config.reason_text_change_every == 0):
                current_reason_text = build_reason_text(random_gen)

            reason0_timestamp_ms = (
                config.start_timestamp_ms + message_index * config.reason0_step_ms
            )
            message = build_message(
                topic=random_gen.choice(topic_pool),
                reason_text=current_reason_text,
                base_timestamp_ms=reason0_timestamp_ms,
                reason_count=config.reason_count,
                random_gen=random_gen,
                fixed_value=config.fixed_value,
            )
            output_stream.write(
                json.dumps(build_envelope(message), separators=(",", ":")) + "\n"
            )


def main() -> int:
    config = parse_args()
    generate_messages(config)
    print(
        "generated"
        f" messages={config.message_count}"
        f" topics={config.topic_count}"
        f" reasonEntriesPerMessage={config.reason_count}"
        f" reason0StepMs={config.reason0_step_ms}"
        f" reasonTextChangeEvery={config.reason_text_change_every}"
        f" fixedValue={config.fixed_value}"
        f" output={config.output_file}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
