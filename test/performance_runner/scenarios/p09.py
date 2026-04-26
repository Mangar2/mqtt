"""Scenario definition."""

from __future__ import annotations

from ..helpers import *  # noqa: F401,F403
from .. import helpers

def execute(config: helpers.RunnerConfig) -> helpers.ScenarioOutcome:
    parameters = PARAMS_P09[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    root = f"perf/p09/{_unique_suffix()}"
    stable_topic = f"{root}/stable"
    step = _scenario_step_logger("P09")
    client_levels = list(
        _ramp_steps(
            int(parameters["start_clients"]),
            int(parameters["end_clients"]),
            int(parameters["step"]),
        )
    )
    ramp_checkpoints = _progress_checkpoints(len(client_levels))

    step(
        f"profile={config.size_profile} clients={parameters['start_clients']}-{parameters['end_clients']} "
        f"step={parameters['step']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(client_levels)} filters_per_client={parameters['filters_per_client']}")

    churn_clients: list[MqttClient] = []
    stable_sent = 0
    stable_received = 0
    try:
        with _connect_client(config, "p09-stable", timeout=max(config.timeout_seconds, 25.0)) as stable_subscriber:
            stable_subscriber.subscribe(stable_topic, qos=0)
            with _connect_client(config, "p09-pub", timeout=max(config.timeout_seconds, 25.0)) as publisher:
                for level_index, target_clients in enumerate(client_levels, start=1):
                    while len(churn_clients) < target_clients:
                        churn_clients.append(_connect_client(config, f"p09-churn{len(churn_clients)}", timeout=max(config.timeout_seconds, 25.0)))

                    for client_index, churn_client in enumerate(churn_clients):
                        for filter_index in range(int(parameters["filters_per_client"])):
                            topic_filter = f"{root}/c{client_index}/f{filter_index}"
                            churn_client.subscribe(topic_filter, qos=0)
                            churn_client.unsubscribe(topic_filter)
                    if level_index in ramp_checkpoints:
                        progress = int((level_index / float(len(client_levels))) * 100.0)
                        step(f"ramp {progress}% churn_clients={len(churn_clients)}")
                    time.sleep(parameters["ramp_interval_s"])

                hold_deadline = time.monotonic() + max(1.0, hold_seconds)
                stable_per_round = 50
                step(f"hold start stable_per_round={stable_per_round} churn_clients={len(churn_clients)}")
                hold_started = time.monotonic()
                while time.monotonic() < hold_deadline:
                    for client_index, churn_client in enumerate(churn_clients):
                        topic_filter = f"{root}/c{client_index}/hold"
                        churn_client.subscribe(topic_filter, qos=0)
                        churn_client.unsubscribe(topic_filter)

                    for message_index in range(stable_per_round):
                        publisher.publish(stable_topic, f"p09-h-{message_index}".encode("utf-8"), qos=0)
                    stable_sent += stable_per_round
                    stable_received += _drain_until(stable_subscriber, stable_per_round, timeout_seconds=max(1.0, float(parameters["churn_interval_s"])))
                    ratio = (stable_received / stable_sent) if stable_sent > 0 else 0.0
                    step(f"hold sent={stable_sent} received={stable_received} ratio={ratio:.4f}")
                    time.sleep(float(parameters["churn_interval_s"]))

                    if hold_seconds <= 1.0:
                        break
                hold_elapsed = max(1e-6, time.monotonic() - hold_started)
    finally:
        for churn_client in churn_clients:
            try:
                churn_client.disconnect()
            except BaseException:
                pass

    stable_ratio = (stable_received / stable_sent) if stable_sent > 0 else 0.0
    broker_alive = _broker_reachable(config.host, config.port, timeout_seconds=1.0)
    success = broker_alive and stable_ratio >= 0.99
    counters = {
        "stable_sent": stable_sent,
        "stable_received": stable_received,
        "churn_clients_peak": len(churn_clients),
        "broker_alive": 1 if broker_alive else 0,
    }
    summary = f"stable_sent={stable_sent} stable_received={stable_received} ratio={stable_ratio:.4f} broker_alive={broker_alive}"
    step(f"final success={success} ratio={stable_ratio:.4f} broker_alive={broker_alive}")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(stable_received),
        throughput_per_second=_safe_rate(float(stable_received), hold_elapsed),
        throughput_basis_seconds=hold_elapsed,
    )



def definition() -> helpers.ScenarioDef:
    scenario_id = "P09"
    spec = next(item for item in helpers.SCENARIO_SPECS if item.scenario_id == scenario_id)
    return helpers.ScenarioDef(spec=spec, execute=execute)
