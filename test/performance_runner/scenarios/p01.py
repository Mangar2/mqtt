"""Scenario definition."""

from __future__ import annotations

from ..helpers import *  # noqa: F401,F403
from .. import helpers

def execute(config: helpers.RunnerConfig) -> helpers.ScenarioOutcome:
    profile_rate_ranges: dict[str, tuple[float, float]] = {
        "small": (100.0, 2000.0),
        "middle": (1000.0, 20000.0),
        "large": (10000.0, 100000.0),
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
    topic = f"perf/p01/{_unique_suffix()}"
    step = _scenario_step_logger("P01")

    def _mmss(seconds_value: float) -> str:
        total_seconds = max(0, int(round(seconds_value)))
        minutes = total_seconds // 60
        seconds = total_seconds % 60
        return f"{minutes}:{seconds:02d}"

    step(
        f"duration={duration_seconds:.0f}s ramp={ramp_start_rate:.0f}->{ramp_end_rate:.0f}/s "
        f"ramp_step={rate_step_seconds:.0f}s hold={hold_duration_seconds:.0f}s report={report_interval_seconds:.0f}s"
    )

    sent_total = 0
    received_total = 0
    ramp_stopped = False
    ramp_stopped_at_rate = 0.0

    with _connect_client(config, "p01-sub", timeout=max(config.timeout_seconds, 20.0)) as subscriber:
        subscriber.subscribe(topic, qos=0)
        with _connect_client(config, "p01-pub", timeout=max(config.timeout_seconds, 20.0)) as publisher:
            started = time.monotonic()
            ended = started + duration_seconds
            block_start = started
            block_end = min(ended, block_start + report_interval_seconds)
            block_sent = 0
            block_received = 0
            current_rate = ramp_start_rate
            current_interval = 1.0 / current_rate
            sequence = 1

            while time.monotonic() < ended:
                now = time.monotonic()
                block_elapsed = max(0.0, min(now, block_end) - block_start)
                required_sent_until_now = int(math.floor((block_elapsed / current_interval) + 1e-9))
                messages_to_send_now = max(0, required_sent_until_now - block_sent)

                for _ in range(messages_to_send_now):
                    publisher.publish(
                        topic,
                        f"p01:{sequence}".encode("utf-8"),
                        qos=0,
                        wait_for_qos0_publish=False,
                    )
                    sent_total += 1
                    block_sent += 1
                    sequence += 1

                drained = subscriber.drain_available_messages(limit=4096)
                if drained:
                    received_count = len(drained)
                    received_total += received_count
                    block_received += received_count

                now = time.monotonic()
                while now >= block_end and block_start < ended:
                    interval_elapsed = max(1e-6, block_end - block_start)
                    send_rate = _safe_rate(float(block_sent), interval_elapsed)
                    receive_rate = _safe_rate(float(block_received), interval_elapsed)
                    stamp = _mmss(block_end - started)
                    step(
                        f"{stamp:>5} send: {block_sent:>5d} {send_rate:>6.1f}/s "
                        f"recv: {block_received:>5d} {receive_rate:>6.1f}/s"
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
                    block_received = 0
                    block_start = block_end
                    block_end = min(ended, block_start + report_interval_seconds)
                    now = time.monotonic()

                time.sleep(0.001)

            flush_deadline = time.monotonic() + 1.0
            while time.monotonic() < flush_deadline:
                drained = subscriber.drain_available_messages(limit=4096)
                if not drained:
                    time.sleep(0.005)
                    continue
                received_total += len(drained)

            elapsed = max(1e-6, time.monotonic() - started)

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
    send_rate_total = _safe_rate(float(sent_total), elapsed)
    recv_rate_total = _safe_rate(float(received_total), elapsed)
    delivery_ratio_total = (float(received_total) / float(sent_total)) if sent_total > 0 else 0.0

    counters = {
        "planned_sent": planned_sent,
        "sent": sent_total,
        "received": received_total,
        "missing": max(0, sent_total - received_total),
        "ramp_stopped": int(ramp_stopped),
        "ramp_stopped_at_rate": ramp_stopped_at_rate,
    }
    summary = (
        f"duration={elapsed:.3f}s planned_sent={planned_sent} sent={sent_total} received={received_total} "
        f"send_rate={send_rate_total:.2f}/s recv_rate={recv_rate_total:.2f}/s delivery_ratio={delivery_ratio_total:.4f} "
        f"ramp_stopped={ramp_stopped}"
    )
    final_stamp = _mmss(elapsed)
    step(
        f"{final_stamp:>5} send: {sent_total:>5d} {send_rate_total:>6.1f}/s "
        f"recv: {received_total:>5d} {recv_rate_total:>6.1f}/s"
    )
    return ScenarioOutcome(
        success=sent_total > 0,
        summary=summary,
        counters=counters,
        work_units=float(received_total),
        throughput_per_second=recv_rate_total,
        throughput_basis_seconds=elapsed,
    )



def definition() -> helpers.ScenarioDef:
    scenario_id = "P01"
    spec = next(item for item in helpers.SCENARIO_SPECS if item.scenario_id == scenario_id)
    return helpers.ScenarioDef(spec=spec, execute=execute)
