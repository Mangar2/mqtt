"""Scenario definition."""

from __future__ import annotations

from ..helpers import *  # noqa: F401,F403
from .. import helpers

def execute(config: helpers.RunnerConfig) -> helpers.ScenarioOutcome:
    parameters = PARAMS_P10[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    root = f"perf/p10/{_unique_suffix()}"
    will_topic = f"{root}/will"
    step = _scenario_step_logger("P10")
    client_levels = list(
        _ramp_steps(
            int(parameters["start_clients"]),
            int(parameters["end_clients"]),
            int(parameters["step"]),
        )
    )
    ramp_checkpoints = _progress_checkpoints(len(client_levels))

    step(
        f"profile={config.size_profile} will_clients={parameters['start_clients']}-{parameters['end_clients']} "
        f"step={parameters['step']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(client_levels)} will_qos={parameters['will_qos']}")

    hold_expected = 0
    hold_received = 0
    with _connect_client(config, "p10-watch", timeout=max(config.timeout_seconds, 25.0)) as watcher:
        watcher.subscribe(will_topic, qos=int(parameters["will_qos"]))

        for level_index, client_count in enumerate(client_levels, start=1):
            will_clients: list[MqttClient] = []
            for index in range(client_count):
                client = MqttClient(timeout_seconds=max(config.timeout_seconds, 25.0))
                client.set_will(
                    topic=will_topic,
                    payload=f"p10-r-{client_count}-{index}".encode("utf-8"),
                    qos=int(parameters["will_qos"]),
                    delay=int(parameters["will_delay_s"]),
                )
                client.connect(config.host, config.port, client_id=f"p10-r-{client_count}-{index}-{_unique_suffix()}")
                will_clients.append(client)

            for client in will_clients:
                _abrupt_disconnect(client)
            delivered = _drain_until(watcher, client_count, timeout_seconds=max(2.0, parameters["ramp_interval_s"] * 2.0))
            if level_index in ramp_checkpoints:
                progress = int((level_index / float(len(client_levels))) * 100.0)
                step(f"ramp {progress}% disconnected={client_count} wills={delivered}/{client_count}")
            time.sleep(parameters["ramp_interval_s"])

        hold_start = time.monotonic()
        hold_iteration = 0
        step(f"hold start end_clients={parameters['end_clients']}")
        while time.monotonic() - hold_start < max(1.0, hold_seconds):
            hold_clients: list[MqttClient] = []
            for index in range(int(parameters["end_clients"])):
                client = MqttClient(timeout_seconds=max(config.timeout_seconds, 25.0))
                client.set_will(
                    topic=will_topic,
                    payload=f"p10-h-{hold_iteration}-{index}".encode("utf-8"),
                    qos=int(parameters["will_qos"]),
                    delay=int(parameters["will_delay_s"]),
                )
                client.connect(config.host, config.port, client_id=f"p10-h-{hold_iteration}-{index}-{_unique_suffix()}")
                hold_clients.append(client)

            for client in hold_clients:
                _abrupt_disconnect(client)

            expected_this_round = int(parameters["end_clients"])
            hold_expected += expected_this_round
            hold_received += _drain_until(watcher, expected_this_round, timeout_seconds=max(2.0, hold_seconds / 2.0))
            ratio = (hold_received / hold_expected) if hold_expected > 0 else 0.0
            step(f"hold iter={hold_iteration + 1} received={hold_received}/{hold_expected} ratio={ratio:.4f}")
            hold_iteration += 1
            if hold_seconds <= 1.0:
                break

    ratio = (hold_received / hold_expected) if hold_expected > 0 else 0.0
    success = ratio >= 0.99
    counters = {
        "hold_expected": hold_expected,
        "hold_received": hold_received,
        "hold_missing": max(0, hold_expected - hold_received),
    }
    summary = f"hold_expected={hold_expected} hold_received={hold_received} ratio={ratio:.4f}"
    step(f"final success={success} ratio={ratio:.4f}")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(hold_received),
        throughput_per_second=_safe_rate(float(hold_received), max(1e-6, time.monotonic() - hold_start)),
        throughput_basis_seconds=max(1e-6, time.monotonic() - hold_start),
    )



def definition() -> helpers.ScenarioDef:
    scenario_id = "P10"
    spec = next(item for item in helpers.SCENARIO_SPECS if item.scenario_id == scenario_id)
    return helpers.ScenarioDef(spec=spec, execute=execute)
