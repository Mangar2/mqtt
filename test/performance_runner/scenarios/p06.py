"""Scenario definition."""

from __future__ import annotations

from ..helpers import *  # noqa: F401,F403
from .. import helpers

def execute(config: helpers.RunnerConfig) -> helpers.ScenarioOutcome:
    parameters = PARAMS_P06[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    topic = f"perf/p06/{_unique_suffix()}"
    step = _scenario_step_logger("P06")
    payload_levels = list(
        _ramp_steps(
            int(parameters["start_payload_bytes"]),
            int(parameters["end_payload_bytes"]),
            int(parameters["step_bytes"]),
        )
    )
    ramp_checkpoints = _progress_checkpoints(len(payload_levels))

    step(
        f"profile={config.size_profile} payload={parameters['start_payload_bytes']}-{parameters['end_payload_bytes']} "
        f"step={parameters['step_bytes']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(payload_levels)} burst={parameters['burst_count']}")

    qos = int(parameters["qos"])
    with _connect_client(config, "p06-sub", timeout=max(config.timeout_seconds, 30.0)) as subscriber:
        subscriber.subscribe(topic, qos=qos)
        with _connect_client(config, "p06-pub", timeout=max(config.timeout_seconds, 30.0)) as publisher:
            for level_index, payload_size in enumerate(payload_levels, start=1):
                payload = (b"r" * payload_size)
                publisher.publish(topic, payload, qos=qos)
                _drain_until(subscriber, 1, timeout_seconds=max(2.0, parameters["ramp_interval_s"]))
                if level_index in ramp_checkpoints:
                    progress = int((level_index / float(len(payload_levels))) * 100.0)
                    step(f"ramp {progress}% payload_bytes={payload_size}")
                time.sleep(parameters["ramp_interval_s"])

            burst_count = int(parameters["burst_count"])
            peak_payload = b"h" * int(parameters["end_payload_bytes"])
            step(f"hold burst start count={burst_count} payload_bytes={parameters['end_payload_bytes']}")
            hold_start = time.monotonic()
            for _index in range(burst_count):
                publisher.publish(topic, peak_payload, qos=qos)
            hold_delivered = _drain_until(subscriber, burst_count, timeout_seconds=max(hold_seconds, 2.0))
            hold_elapsed = max(1e-6, time.monotonic() - hold_start)

    delivered_bytes = hold_delivered * int(parameters["end_payload_bytes"])
    throughput_bytes_s = delivered_bytes / hold_elapsed
    success = hold_delivered == int(parameters["burst_count"])
    counters = {
        "hold_burst": int(parameters["burst_count"]),
        "hold_delivered": hold_delivered,
        "payload_bytes": int(parameters["end_payload_bytes"]),
        "delivered_bytes": delivered_bytes,
    }
    summary = (
        f"burst={parameters['burst_count']} delivered={hold_delivered} payload={parameters['end_payload_bytes']} "
        f"bytes throughput={throughput_bytes_s:.2f}"
    )
    step(f"final success={success} delivered={hold_delivered}/{burst_count} throughput={throughput_bytes_s:.2f}B/s")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(delivered_bytes),
        throughput_per_second=_safe_rate(float(delivered_bytes), hold_elapsed),
        throughput_basis_seconds=hold_elapsed,
    )



def definition() -> helpers.ScenarioDef:
    scenario_id = "P06"
    spec = next(item for item in helpers.SCENARIO_SPECS if item.scenario_id == scenario_id)
    return helpers.ScenarioDef(spec=spec, execute=execute)
