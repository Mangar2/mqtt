"""Scenario definition."""

from __future__ import annotations

from ..helpers import *  # noqa: F401,F403
from .. import helpers

def execute(config: helpers.RunnerConfig) -> helpers.ScenarioOutcome:
    parameters = PARAMS_P08[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    topic = f"perf/p08/{_unique_suffix()}"
    client_id = f"p08-sub-{_unique_suffix()}"
    step = _scenario_step_logger("P08")
    queue_levels = list(
        _ramp_steps(
            int(parameters["start_queued"]),
            int(parameters["end_queued"]),
            int(parameters["step"]),
        )
    )
    ramp_checkpoints = _progress_checkpoints(len(queue_levels))

    step(
        f"profile={config.size_profile} queued={parameters['start_queued']}-{parameters['end_queued']} "
        f"step={parameters['step']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(queue_levels)} drain_timeout={parameters['drain_timeout_s']}s")

    connect_props = Properties(PacketTypes.CONNECT)
    setattr(connect_props, "SessionExpiryInterval", int(parameters["session_expiry_s"]))

    with _connect_client(config, "p08-init", clean_start=True, client_id=client_id, properties=connect_props) as initial_subscriber:
        initial_subscriber.subscribe(topic, qos=1)

    missing_total = 0
    hold_missing = 0
    hold_expected = 0
    hold_delivered = 0

    with _connect_client(config, "p08-pub", timeout=max(config.timeout_seconds, 25.0)) as publisher:
        for level_index, queued_count in enumerate(queue_levels, start=1):
            for index in range(queued_count):
                publisher.publish(topic, f"p08-r-{queued_count}-{index}".encode("utf-8"), qos=1)

            with _connect_client(config, "p08-resume", clean_start=False, client_id=client_id, properties=connect_props) as resumed:
                drained = _drain_until(resumed, queued_count, timeout_seconds=float(parameters["drain_timeout_s"]))
            missing_total += max(0, queued_count - drained)
            if level_index in ramp_checkpoints:
                progress = int((level_index / float(len(queue_levels))) * 100.0)
                step(f"ramp {progress}% queued={queued_count} drained={drained}/{queued_count}")
            time.sleep(parameters["ramp_interval_s"])

        hold_rounds = max(1, int(math.ceil(hold_seconds / max(1.0, parameters["ramp_interval_s"]))))
        hold_checkpoints = _progress_checkpoints(hold_rounds)
        step(f"hold start rounds={hold_rounds} queued={parameters['end_queued']}")
        hold_started = time.monotonic()
        for round_index in range(hold_rounds):
            queued_count = int(parameters["end_queued"])
            for index in range(queued_count):
                publisher.publish(topic, f"p08-h-{round_index}-{index}".encode("utf-8"), qos=1)

            with _connect_client(config, "p08-hold", clean_start=False, client_id=client_id, properties=connect_props) as resumed:
                drained = _drain_until(resumed, queued_count, timeout_seconds=float(parameters["drain_timeout_s"]))
            hold_expected += queued_count
            hold_delivered += drained
            hold_missing += max(0, queued_count - drained)
            if (round_index + 1) in hold_checkpoints:
                step(
                    f"hold {(round_index + 1)}/{hold_rounds} delivered={hold_delivered}/{hold_expected} "
                    f"missing={hold_missing}"
                )
        hold_elapsed = max(1e-6, time.monotonic() - hold_started)

    success = hold_missing == 0 and missing_total == 0
    counters = {
        "missing_total": missing_total,
        "hold_expected": hold_expected,
        "hold_delivered": hold_delivered,
        "hold_missing": hold_missing,
    }
    summary = (
        f"missing_total={missing_total} hold_expected={hold_expected} "
        f"hold_delivered={hold_delivered} hold_missing={hold_missing}"
    )
    step(f"final success={success} hold_missing={hold_missing} ramp_missing={missing_total}")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(hold_delivered),
        throughput_per_second=_safe_rate(float(hold_delivered), hold_elapsed),
        throughput_basis_seconds=hold_elapsed,
    )



def definition() -> helpers.ScenarioDef:
    scenario_id = "P08"
    spec = next(item for item in helpers.SCENARIO_SPECS if item.scenario_id == scenario_id)
    return helpers.ScenarioDef(spec=spec, execute=execute)
