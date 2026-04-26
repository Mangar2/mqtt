"""Scenario definition."""

from __future__ import annotations

from ..helpers import *  # noqa: F401,F403
from .. import helpers

def execute(config: helpers.RunnerConfig) -> helpers.ScenarioOutcome:
    parameters = PARAMS_P04[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    topic = f"perf/p04/{_unique_suffix()}"
    step = _scenario_step_logger("P04")
    ramp_targets = list(
        _ramp_steps(
            int(parameters["start_subscribers"]),
            int(parameters["end_subscribers"]),
            int(parameters["step"]),
        )
    )
    ramp_checkpoints = _progress_checkpoints(len(ramp_targets))

    step(
        f"profile={config.size_profile} subscribers={parameters['start_subscribers']}-{parameters['end_subscribers']} "
        f"step={parameters['step']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(ramp_targets)} msg_per_step={parameters['msg_per_step']}")

    subscribers: list[MqttClient] = []
    hold_expected = 0
    hold_delivered = 0
    try:
        with _connect_client(config, "p04-pub", timeout=max(config.timeout_seconds, 20.0)) as publisher:
            subscriber_target = int(parameters["start_subscribers"])
            while len(subscribers) < subscriber_target:
                subscriber = _connect_client(config, f"p04-sub{len(subscribers)}", timeout=max(config.timeout_seconds, 20.0))
                subscriber.subscribe(topic, qos=int(parameters["qos"]))
                subscribers.append(subscriber)

            for level_index, subscriber_target in enumerate(ramp_targets, start=1):
                while len(subscribers) < subscriber_target:
                    subscriber = _connect_client(config, f"p04-sub{len(subscribers)}", timeout=max(config.timeout_seconds, 20.0))
                    subscriber.subscribe(topic, qos=int(parameters["qos"]))
                    subscribers.append(subscriber)

                for message_index in range(int(parameters["msg_per_step"])):
                    publisher.publish(topic, f"p04-r-{subscriber_target}-{message_index}".encode("utf-8"), qos=int(parameters["qos"]))
                for subscriber in subscribers:
                    _drain_until(subscriber, int(parameters["msg_per_step"]), timeout_seconds=max(2.0, parameters["ramp_interval_s"]))
                if level_index in ramp_checkpoints:
                    progress = int((level_index / float(len(ramp_targets))) * 100.0)
                    step(f"ramp {progress}% subscribers={len(subscribers)}")
                time.sleep(parameters["ramp_interval_s"])

            hold_rounds = max(1, int(math.ceil(hold_seconds)))
            hold_messages = int(parameters["msg_per_step"])
            hold_checkpoints = _progress_checkpoints(hold_rounds)
            step(f"hold start rounds={hold_rounds} subscribers={len(subscribers)}")
            hold_started = time.monotonic()
            for round_index in range(hold_rounds):
                for message_index in range(hold_messages):
                    publisher.publish(topic, f"p04-h-{round_index}-{message_index}".encode("utf-8"), qos=int(parameters["qos"]))
                hold_expected += hold_messages * len(subscribers)
                for subscriber in subscribers:
                    hold_delivered += _drain_until(subscriber, hold_messages, timeout_seconds=max(2.0, hold_seconds / hold_rounds))
                if (round_index + 1) in hold_checkpoints:
                    ratio = (hold_delivered / hold_expected) if hold_expected > 0 else 0.0
                    step(f"hold {(round_index + 1)}/{hold_rounds} delivered={hold_delivered}/{hold_expected} ratio={ratio:.4f}")
            hold_elapsed = max(1e-6, time.monotonic() - hold_started)
    finally:
        for subscriber in subscribers:
            try:
                subscriber.disconnect()
            except BaseException:
                pass

    ratio = (hold_delivered / hold_expected) if hold_expected > 0 else 0.0
    success = ratio >= 0.99
    counters = {
        "hold_expected": hold_expected,
        "hold_delivered": hold_delivered,
        "subscribers_peak": len(subscribers),
    }
    summary = f"hold_expected={hold_expected} hold_delivered={hold_delivered} ratio={ratio:.4f}"
    step(f"final success={success} ratio={ratio:.4f} peak_subscribers={len(subscribers)}")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(hold_delivered),
        throughput_per_second=_safe_rate(float(hold_delivered), hold_elapsed),
        throughput_basis_seconds=hold_elapsed,
    )



def definition() -> helpers.ScenarioDef:
    scenario_id = "P04"
    spec = next(item for item in helpers.SCENARIO_SPECS if item.scenario_id == scenario_id)
    return helpers.ScenarioDef(spec=spec, execute=execute)
