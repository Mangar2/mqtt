"""Scenario definition."""

from __future__ import annotations

from ..helpers import *  # noqa: F401,F403
from .. import helpers

def execute(config: helpers.RunnerConfig) -> helpers.ScenarioOutcome:
    profile_rate_ranges: dict[str, tuple[float, float]] = {
        "small": (10.0, 2000.0),
        "middle": (100.0, 20000.0),
        "large": (1000.0, 100000.0),
    }
    ramp_start_rate, ramp_end_rate = profile_rate_ranges[config.size_profile]
    ramp_duration_seconds = 90.0
    hold_duration_seconds = 30.0
    duration_seconds = ramp_duration_seconds + hold_duration_seconds
    rate_step_seconds = 10.0
    ramp_step_count = int(ramp_duration_seconds / rate_step_seconds)
    rate_step_per_level = (ramp_end_rate - ramp_start_rate) / float(ramp_step_count)
    overload_deficit_threshold = 0.30
    report_interval_seconds = 10.0
    max_messages_per_publisher_per_second = 2000
    topic_prefix = f"perf/p02/{_unique_suffix()}"
    step = _scenario_step_logger("P02")

    planned_sent = int(
        sum(
            (
                ramp_start_rate + (rate_step_per_level * float(level_index))
            )
            * rate_step_seconds
            for level_index in range(ramp_step_count)
        )
        + (ramp_end_rate * hold_duration_seconds)
    )

    def _mmss(seconds_value: float) -> str:
        total_seconds = max(0, int(round(seconds_value)))
        minutes = total_seconds // 60
        seconds = total_seconds % 60
        return f"{minutes}:{seconds:02d}"

    step(
        f"duration={duration_seconds:.0f}s ramp={ramp_start_rate:.0f}->{ramp_end_rate:.0f}/s "
        f"ramp_step={rate_step_seconds:.0f}s hold={hold_duration_seconds:.0f}s report={report_interval_seconds:.0f}s"
    )
    step(
        f"publish-sharding paired_clients=1..N max_rate_per_client={max_messages_per_publisher_per_second}/s "
        f"planned_sent={planned_sent}"
    )

    sent_total = 0
    ack_total = 0
    ack_non_success_total = 0
    received_total = 0
    received_first_total = 0
    received_dup_total = 0
    ramp_stopped = False
    ramp_stopped_at_rate = 0.0
    pending_publish_mids: set[tuple[int, int]] = set()
    ack_reason_code_counts: Counter[int] = Counter()
    publish_error_total = 0
    publish_error_rc_counts: Counter[int] = Counter()
    publisher_sent_counts = [0]
    next_publisher_index = 0
    seen_received_sequences: set[int] = set()

    def _parse_publish_error_rc(error_text: str) -> int | None:
        rc_marker = "rc="
        marker_index = error_text.find(rc_marker)
        if marker_index < 0:
            return None
        value_start = marker_index + len(rc_marker)
        value_end = value_start
        while value_end < len(error_text) and error_text[value_end].isdigit():
            value_end += 1
        if value_end == value_start:
            return None
        try:
            return int(error_text[value_start:value_end])
        except ValueError:
            return None

    def _format_code_counter(counter: Counter[int]) -> str:
        if not counter:
            return "none"
        return ",".join(
            f"{code}:{count}" for code, count in sorted(counter.items())
        )

    def _extract_p02_sequence(message_payload: bytes) -> int | None:
        try:
            payload_text = message_payload.decode("utf-8", errors="ignore")
        except BaseException:
            return None
        if not payload_text.startswith("p02:"):
            return None
        try:
            return int(payload_text.split(":", 1)[1])
        except (ValueError, IndexError):
            return None

    with contextlib.ExitStack() as client_stack:
        topics = [f"{topic_prefix}/0"]
        subscribers = [
            client_stack.enter_context(
                _connect_client(
                    config,
                    "p02-sub",
                    timeout=max(config.timeout_seconds, 20.0),
                    client_id=f"p02-sub-0-{_unique_suffix()}",
                )
            )
        ]
        subscribers[0].subscribe(topics[0], qos=1)

        publishers = [
            client_stack.enter_context(
                _connect_client(
                    config,
                    "p02-pub",
                    timeout=max(config.timeout_seconds, 20.0),
                    client_id=f"p02-pub-0-{_unique_suffix()}",
                )
            )
        ]

        started = time.monotonic()
        ended = started + duration_seconds
        block_start = started
        block_end = min(ended, block_start + report_interval_seconds)
        block_sent = 0
        block_acked = 0
        block_received = 0
        block_received_first = 0
        block_received_dup = 0
        current_rate = ramp_start_rate
        current_interval = 1.0 / current_rate
        sequence = 1

        while time.monotonic() < ended:
            now = time.monotonic()
            block_elapsed = max(0.0, min(now, block_end) - block_start)
            required_sent_until_now = int(math.floor((block_elapsed / current_interval) + 1e-9))
            messages_to_send_now = max(0, required_sent_until_now - block_sent)

            active_publisher_clients = max(
                1,
                int(math.ceil(float(current_rate) / float(max_messages_per_publisher_per_second))),
            )
            while len(publishers) < active_publisher_clients:
                new_index = len(publishers)
                new_topic = f"{topic_prefix}/{new_index}"
                new_subscriber = client_stack.enter_context(
                    _connect_client(
                        config,
                        "p02-sub",
                        timeout=max(config.timeout_seconds, 20.0),
                        client_id=f"p02-sub-{new_index}-{_unique_suffix()}",
                    )
                )
                new_subscriber.subscribe(new_topic, qos=1)
                subscribers.append(new_subscriber)
                topics.append(new_topic)

                new_publisher = client_stack.enter_context(
                    _connect_client(
                        config,
                        "p02-pub",
                        timeout=max(config.timeout_seconds, 20.0),
                        client_id=f"p02-pub-{new_index}-{_unique_suffix()}",
                    )
                )
                publishers.append(new_publisher)
                publisher_sent_counts.append(0)

            for _ in range(messages_to_send_now):
                chosen_publisher_index = next_publisher_index % active_publisher_clients
                next_publisher_index = (next_publisher_index + 1) % active_publisher_clients

                publisher = publishers[chosen_publisher_index]
                paired_subscriber = subscribers[chosen_publisher_index]
                target_topic = topics[chosen_publisher_index]
                try:
                    publish_mid = int(
                        publisher.publish(
                            target_topic,
                            f"p02:{sequence}".encode("utf-8"),
                            qos=1,
                            wait_for_qos1_publish=False,
                        )
                    )
                except RuntimeError as runtime_error:
                    publish_error_total += 1
                    publish_error_rc = _parse_publish_error_rc(str(runtime_error))
                    if publish_error_rc is not None:
                        publish_error_rc_counts[publish_error_rc] += 1

                    raise RuntimeError(
                        _format_p02_publish_abort_summary(
                            runtime_error,
                            publisher=publisher,
                            publisher_index=chosen_publisher_index,
                            publisher_clients_total=len(publishers),
                            publisher_sent_count=publisher_sent_counts[chosen_publisher_index],
                            publisher_rate_cap_per_second=max_messages_per_publisher_per_second,
                            subscriber=paired_subscriber,
                            sent_total=sent_total,
                            ack_total=ack_total,
                            received_total=received_total,
                            pending_publish_count=len(pending_publish_mids),
                            current_rate=current_rate,
                        )
                        + f" publish_error_total={publish_error_total}"
                        + f" publish_error_rc={_format_code_counter(publish_error_rc_counts)}"
                    ) from runtime_error
                pending_publish_mids.add((chosen_publisher_index, publish_mid))
                sent_total += 1
                block_sent += 1
                publisher_sent_counts[chosen_publisher_index] += 1
                sequence += 1

            for publisher_index, publisher in enumerate(publishers):
                completed_mids = publisher.drain_published_mids(limit=65536)
                if not completed_mids:
                    continue
                for mid_value, reason_value in completed_mids.items():
                    reason_code_int = int(reason_value)
                    ack_reason_code_counts[reason_code_int] += 1
                    pending_publish_mids.discard((publisher_index, mid_value))
                    if reason_code_int in (0x00, 0x10):
                        ack_total += 1
                        block_acked += 1
                    else:
                        ack_non_success_total += 1

            for subscriber in subscribers:
                drained = subscriber.drain_available_messages(limit=4096)
                if not drained:
                    continue
                received_count = len(drained)
                received_total += received_count
                block_received += received_count
                for received_message in drained:
                    if bool(getattr(received_message, "dup", False)):
                        received_dup_total += 1
                        block_received_dup += 1
                    sequence_number = _extract_p02_sequence(received_message.payload)
                    if sequence_number is None:
                        continue
                    if sequence_number in seen_received_sequences:
                        continue
                    seen_received_sequences.add(sequence_number)
                    received_first_total += 1
                    block_received_first += 1

            now = time.monotonic()
            while now >= block_end and block_start < ended:
                interval_elapsed = max(1e-6, block_end - block_start)
                send_rate = _safe_rate(float(block_sent), interval_elapsed)
                broker_rate = _safe_rate(float(block_acked), interval_elapsed)
                receive_rate = _safe_rate(float(block_received), interval_elapsed)
                receive_first_rate = _safe_rate(float(block_received_first), interval_elapsed)
                receive_dup_rate = _safe_rate(float(block_received_dup), interval_elapsed)
                stamp = _mmss(block_end - started)
                step(
                    f"{stamp:>5} sent: {block_sent:>5d} {send_rate:>6.1f}/s "
                    f"(ack {broker_rate:>6.1f}/s) "
                    f"recv: {block_received:>5d} {receive_rate:>6.1f}/s "
                    f"(first {block_received_first:>5d} {receive_first_rate:>6.1f}/s dup {block_received_dup:>5d} {receive_dup_rate:>6.1f}/s)"
                )

                if (not ramp_stopped) and block_end <= (started + ramp_duration_seconds):
                    deficit_ratio = (
                        (float(block_sent - block_received) / float(block_sent))
                        if block_sent > 0
                        else 0.0
                    )
                    if deficit_ratio >= overload_deficit_threshold:
                        ramp_stopped = True
                        ramp_stopped_at_rate = current_rate
                    elif current_rate < ramp_end_rate:
                        current_rate = min(ramp_end_rate, current_rate + rate_step_per_level)
                        current_interval = 1.0 / current_rate

                block_sent = 0
                block_acked = 0
                block_received = 0
                block_received_first = 0
                block_received_dup = 0
                block_start = block_end
                block_end = min(ended, block_start + report_interval_seconds)
                now = time.monotonic()

            time.sleep(0.001)

        flush_deadline = time.monotonic() + 1.0
        while time.monotonic() < flush_deadline:
            for publisher_index, publisher in enumerate(publishers):
                completed_mids = publisher.drain_published_mids(limit=65536)
                if not completed_mids:
                    continue
                for mid_value, reason_value in completed_mids.items():
                    reason_code_int = int(reason_value)
                    ack_reason_code_counts[reason_code_int] += 1
                    pending_publish_mids.discard((publisher_index, mid_value))
                    if reason_code_int in (0x00, 0x10):
                        ack_total += 1
                    else:
                        ack_non_success_total += 1

            drained_any = False
            for subscriber in subscribers:
                drained = subscriber.drain_available_messages(limit=4096)
                if not drained:
                    continue
                drained_any = True
                received_total += len(drained)
                for received_message in drained:
                    if bool(getattr(received_message, "dup", False)):
                        received_dup_total += 1
                    sequence_number = _extract_p02_sequence(received_message.payload)
                    if sequence_number is None:
                        continue
                    if sequence_number in seen_received_sequences:
                        continue
                    seen_received_sequences.add(sequence_number)
                    received_first_total += 1

            if not drained_any:
                time.sleep(0.005)
                continue

        elapsed = max(1e-6, time.monotonic() - started)
    send_rate_total = _safe_rate(float(sent_total), elapsed)
    recv_rate_total = _safe_rate(float(received_total), elapsed)
    recv_first_rate_total = _safe_rate(float(received_first_total), elapsed)
    recv_dup_rate_total = _safe_rate(float(received_dup_total), elapsed)
    ack_rate_total = _safe_rate(float(ack_total), elapsed)
    delivery_ratio_total = (float(received_total) / float(ack_total)) if ack_total > 0 else 0.0
    delivery_ratio_first_total = (float(received_first_total) / float(ack_total)) if ack_total > 0 else 0.0
    ack_ratio_total = (float(ack_total) / float(sent_total)) if sent_total > 0 else 0.0

    success = delivery_ratio_total >= 0.99 and ack_ratio_total >= 0.99
    counters = {
        "planned_sent": planned_sent,
        "sent": sent_total,
        "publisher_clients_used": len(publishers),
        "max_messages_per_publisher_per_second": max_messages_per_publisher_per_second,
        "broker_sent": ack_total,
        "ack_non_success": ack_non_success_total,
        "ack_reason_code_counts": {
            str(code): count for code, count in sorted(ack_reason_code_counts.items())
        },
        "received": received_total,
        "received_first": received_first_total,
        "received_dup": received_dup_total,
        "missing": max(0, ack_total - received_total),
        "missing_first": max(0, ack_total - received_first_total),
        "unacked": max(0, sent_total - ack_total),
        "publish_error_total": publish_error_total,
        "publish_error_rc_counts": {
            str(code): count for code, count in sorted(publish_error_rc_counts.items())
        },
        "pending_publish_mids": len(pending_publish_mids),
        "ramp_stopped": int(ramp_stopped),
        "ramp_stopped_at_rate": ramp_stopped_at_rate,
    }
    summary = (
        f"duration={elapsed:.3f}s planned_sent={planned_sent} sent={sent_total} pub_clients={len(publishers)} cap_per_client_rate={max_messages_per_publisher_per_second}/s "
        f"broker_sent={ack_total} received={received_total} received_first={received_first_total} received_dup={received_dup_total} "
        f"broker_rate={ack_rate_total:.2f}/s recv_rate={recv_rate_total:.2f}/s recv_first_rate={recv_first_rate_total:.2f}/s recv_dup_rate={recv_dup_rate_total:.2f}/s "
        f"ack_ratio={ack_ratio_total:.4f} delivery_ratio={delivery_ratio_total:.4f} delivery_ratio_first={delivery_ratio_first_total:.4f} ramp_stopped={ramp_stopped} "
        f"ack_reason_codes={_format_code_counter(ack_reason_code_counts)} "
        f"publish_error_rc={_format_code_counter(publish_error_rc_counts)}"
    )
    final_stamp = _mmss(elapsed)
    step(
        f"{final_stamp:>5} sent: {sent_total:>5d} {send_rate_total:>6.1f}/s "
        f"(ack {ack_rate_total:>6.1f}/s) "
        f"recv: {received_total:>5d} {recv_rate_total:>6.1f}/s "
        f"(first {received_first_total:>5d} {recv_first_rate_total:>6.1f}/s dup {received_dup_total:>5d} {recv_dup_rate_total:>6.1f}/s)"
    )
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(received_total),
        throughput_per_second=recv_rate_total,
        throughput_basis_seconds=elapsed,
    )



def definition() -> helpers.ScenarioDef:
    scenario_id = "P02"
    spec = next(item for item in helpers.SCENARIO_SPECS if item.scenario_id == scenario_id)
    return helpers.ScenarioDef(spec=spec, execute=execute)
