"""Scenario definition."""

from __future__ import annotations

from ..helpers import *  # noqa: F401,F403
from .. import helpers

def execute(config: helpers.RunnerConfig) -> helpers.ScenarioOutcome:
    parameters = PARAMS_P07[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    root = f"perf/p07/{_unique_suffix()}"
    qos = int(parameters["qos"])
    step = _scenario_step_logger("P07")
    ramp_topics = list(
        _ramp_steps(
            int(parameters["start_topics"]),
            int(parameters["end_topics"]),
            int(parameters["step"]),
        )
    )
    ramp_checkpoints = _progress_checkpoints(len(ramp_topics))

    step(
        f"profile={config.size_profile} topics={parameters['start_topics']}-{parameters['end_topics']} "
        f"step={parameters['step']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(ramp_topics)} root={root}")

    created_topics: list[str] = []
    replay_attempts = 0
    replay_ok = 0
    hold_replayed = 0
    hold_expected = 0

    try:
        with _connect_client(config, "p07-pub", timeout=max(config.timeout_seconds, 25.0)) as publisher:
            last_count = 0
            for level_index, topic_count in enumerate(ramp_topics, start=1):
                for index in range(last_count, topic_count):
                    topic = f"{root}/{index}"
                    created_topics.append(topic)
                    publisher.publish(topic, f"ret-{index}".encode("utf-8"), qos=qos, retain=True)
                last_count = topic_count

                replay_attempts += 1
                with _connect_client(config, "p07-ramp-sub", timeout=max(config.timeout_seconds, 25.0)) as ramp_subscriber:
                    ramp_subscriber.subscribe(f"{root}/#", qos=qos)
                    received = _drain_until(ramp_subscriber, topic_count, timeout_seconds=max(3.0, parameters["ramp_interval_s"] * 2.0))
                    if topic_count > 0 and (received / topic_count) >= 0.99:
                        replay_ok += 1
                if level_index in ramp_checkpoints:
                    progress = int((level_index / float(len(ramp_topics))) * 100.0)
                    step(f"ramp {progress}% topics={topic_count} replay_attempts={replay_attempts}")
                time.sleep(parameters["ramp_interval_s"])

            hold_deadline = time.monotonic() + max(1.0, hold_seconds)
            step(f"hold start target_topics={parameters['end_topics']}")
            hold_started = time.monotonic()
            while time.monotonic() < hold_deadline:
                replay_attempts += 1
                hold_expected += int(parameters["end_topics"])
                with _connect_client(config, "p07-hold-sub", timeout=max(config.timeout_seconds, 25.0)) as hold_subscriber:
                    hold_subscriber.subscribe(f"{root}/#", qos=qos)
                    received = _drain_until(hold_subscriber, int(parameters["end_topics"]), timeout_seconds=max(1.5, hold_seconds / 2.0))
                    hold_replayed += received
                    if int(parameters["end_topics"]) > 0 and (received / int(parameters["end_topics"])) >= 0.99:
                        replay_ok += 1
                hold_ratio = (hold_replayed / hold_expected) if hold_expected > 0 else 0.0
                step(
                    f"hold attempt={replay_attempts} replay_ok={replay_ok} "
                    f"replayed={hold_replayed}/{hold_expected} ratio={hold_ratio:.4f}"
                )
                if hold_seconds <= 1.0:
                    break
            hold_elapsed = max(1e-6, time.monotonic() - hold_started)
    finally:
        with _connect_client(config, "p07-clean", timeout=max(config.timeout_seconds, 25.0)) as cleanup_publisher:
            for topic in created_topics:
                cleanup_publisher.publish(topic, b"", qos=qos, retain=True)

    hold_ratio = (hold_replayed / hold_expected) if hold_expected > 0 else 0.0
    success = replay_attempts > 0 and replay_ok == replay_attempts and hold_ratio >= 0.99
    counters = {
        "replay_attempts": replay_attempts,
        "replay_ok": replay_ok,
        "hold_expected": hold_expected,
        "hold_replayed": hold_replayed,
    }
    summary = f"attempts={replay_attempts} replay_ok={replay_ok} hold_ratio={hold_ratio:.4f}"
    step(f"final success={success} attempts={replay_attempts} hold_ratio={hold_ratio:.4f}")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(hold_replayed),
        throughput_per_second=_safe_rate(float(hold_replayed), hold_elapsed),
        throughput_basis_seconds=hold_elapsed,
    )



def definition() -> helpers.ScenarioDef:
    scenario_id = "P07"
    spec = next(item for item in helpers.SCENARIO_SPECS if item.scenario_id == scenario_id)
    return helpers.ScenarioDef(spec=spec, execute=execute)
