"""Scenario definition."""

from __future__ import annotations

from ..helpers import *  # noqa: F401,F403
from .. import helpers

def execute(config: helpers.RunnerConfig) -> helpers.ScenarioOutcome:
    parameters = PARAMS_P05[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    topic = f"perf/p05/{_unique_suffix()}"
    step = _scenario_step_logger("P05")
    ramp_targets = list(
        _ramp_steps(
            int(parameters["start_publishers"]),
            int(parameters["end_publishers"]),
            int(parameters["step"]),
        )
    )
    ramp_checkpoints = _progress_checkpoints(len(ramp_targets))

    step(
        f"profile={config.size_profile} publishers={parameters['start_publishers']}-{parameters['end_publishers']} "
        f"step={parameters['step']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(ramp_targets)} msg_per_pub={parameters['msg_per_publisher_per_step']}")

    publishers: list[MqttClient] = []
    hold_sent = 0
    hold_delivered = 0
    try:
        with _connect_client(config, "p05-sub", timeout=max(config.timeout_seconds, 20.0)) as subscriber:
            subscriber.subscribe(topic, qos=int(parameters["qos"]))

            while len(publishers) < int(parameters["start_publishers"]):
                publishers.append(_connect_client(config, f"p05-pub{len(publishers)}", timeout=max(config.timeout_seconds, 20.0)))

            for level_index, publisher_target in enumerate(ramp_targets, start=1):
                while len(publishers) < publisher_target:
                    publishers.append(_connect_client(config, f"p05-pub{len(publishers)}", timeout=max(config.timeout_seconds, 20.0)))

                messages_per_publisher = int(parameters["msg_per_publisher_per_step"])
                for publisher_index, publisher in enumerate(publishers):
                    for message_index in range(messages_per_publisher):
                        publisher.publish(topic, f"p05-r-{publisher_index}-{message_index}".encode("utf-8"), qos=int(parameters["qos"]))
                expected_step = len(publishers) * messages_per_publisher
                _drain_until(subscriber, expected_step, timeout_seconds=max(2.0, parameters["ramp_interval_s"]))
                if level_index in ramp_checkpoints:
                    progress = int((level_index / float(len(ramp_targets))) * 100.0)
                    step(f"ramp {progress}% publishers={len(publishers)} expected_step={expected_step}")
                time.sleep(parameters["ramp_interval_s"])

            hold_rounds = max(1, int(math.ceil(hold_seconds)))
            messages_per_publisher = int(parameters["msg_per_publisher_per_step"])
            hold_checkpoints = _progress_checkpoints(hold_rounds)
            step(f"hold start rounds={hold_rounds} publishers={len(publishers)}")
            hold_started = time.monotonic()
            for round_index in range(hold_rounds):
                for publisher_index, publisher in enumerate(publishers):
                    for message_index in range(messages_per_publisher):
                        publisher.publish(topic, f"p05-h-{round_index}-{publisher_index}-{message_index}".encode("utf-8"), qos=int(parameters["qos"]))
                expected_round = len(publishers) * messages_per_publisher
                hold_sent += expected_round
                hold_delivered += _drain_until(subscriber, expected_round, timeout_seconds=max(2.0, hold_seconds / hold_rounds))
                if (round_index + 1) in hold_checkpoints:
                    ratio = (hold_delivered / hold_sent) if hold_sent > 0 else 0.0
                    step(f"hold {(round_index + 1)}/{hold_rounds} delivered={hold_delivered}/{hold_sent} ratio={ratio:.4f}")
            hold_elapsed = max(1e-6, time.monotonic() - hold_started)
    finally:
        for publisher in publishers:
            try:
                publisher.disconnect()
            except BaseException:
                pass

    ratio = (hold_delivered / hold_sent) if hold_sent > 0 else 0.0
    success = ratio >= 0.99
    counters = {
        "hold_sent": hold_sent,
        "hold_delivered": hold_delivered,
        "publishers_peak": len(publishers),
    }
    summary = f"hold_sent={hold_sent} hold_delivered={hold_delivered} ratio={ratio:.4f}"
    step(f"final success={success} ratio={ratio:.4f} peak_publishers={len(publishers)}")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(hold_delivered),
        throughput_per_second=_safe_rate(float(hold_delivered), hold_elapsed),
        throughput_basis_seconds=hold_elapsed,
    )



def definition() -> helpers.ScenarioDef:
    scenario_id = "P05"
    spec = next(item for item in helpers.SCENARIO_SPECS if item.scenario_id == scenario_id)
    return helpers.ScenarioDef(spec=spec, execute=execute)
