"""Scenario definition."""

from __future__ import annotations

from ..helpers import *  # noqa: F401,F403
from .. import helpers

def execute(config: helpers.RunnerConfig) -> helpers.ScenarioOutcome:
    parameters = PARAMS_P03[config.size_profile]
    hold_seconds = _effective_hold_seconds(config, parameters["hold_seconds"])
    topic = f"perf/p03/{_unique_suffix()}"
    step = _scenario_step_logger("P03")
    ramp_rates = list(_ramp_steps(parameters["start_msg_s"], parameters["end_msg_s"], parameters["step"]))
    ramp_checkpoints = _progress_checkpoints(len(ramp_rates))

    step(
        f"profile={config.size_profile} ramp={parameters['start_msg_s']}-{parameters['end_msg_s']} "
        f"step={parameters['step']} hold={hold_seconds:.1f}s"
    )
    step(f"ramp plan levels={len(ramp_rates)} topic={topic}")

    hold_sent = 0
    hold_completed = 0
    hold_delivered = 0
    peak_pending = 0

    with _connect_client(config, "p03-sub", timeout=max(config.timeout_seconds, 25.0)) as subscriber:
        subscriber.subscribe(topic, qos=2)
        with _connect_client(config, "p03-pub", timeout=max(config.timeout_seconds, 25.0)) as publisher:
            for rate_index, rate in enumerate(ramp_rates, start=1):
                sent_ramp = 0
                completed_ramp = 0
                level_started = time.monotonic()
                for index in range(rate):
                    sent_ramp += 1
                    reason = int(publisher.publish(topic, f"p03-r-{rate}-{index}".encode("utf-8"), qos=2))
                    if reason in (0x00, 0x10):
                        completed_ramp += 1
                peak_pending = max(peak_pending, sent_ramp - completed_ramp)
                delivered = _drain_until(subscriber, rate, timeout_seconds=max(2.0, parameters["ramp_interval_s"]))
                if rate_index in ramp_checkpoints:
                    progress = int((rate_index / float(len(ramp_rates))) * 100.0)
                    level_elapsed = max(1e-6, time.monotonic() - level_started)
                    actual_rate = float(delivered) / level_elapsed
                    step(
                        f"ramp {progress}% target_msgs_per_step={rate} completed={completed_ramp}/{sent_ramp} "
                        f"delivered={delivered}/{rate} step_elapsed={level_elapsed:.3f}s "
                        f"actual_msgs_per_s={actual_rate:.2f}"
                    )
                time.sleep(parameters["ramp_interval_s"])

            hold_rounds = max(1, int(math.ceil(hold_seconds)))
            hold_rate = int(parameters["end_msg_s"])
            hold_checkpoints = _progress_checkpoints(hold_rounds)
            step(f"hold start rounds={hold_rounds} target_msgs_per_round={hold_rate}")
            hold_started = time.monotonic()
            for round_index in range(hold_rounds):
                sent_round = 0
                completed_round = 0
                for item_index in range(hold_rate):
                    hold_sent += 1
                    sent_round += 1
                    reason = int(publisher.publish(topic, f"p03-h-{round_index}-{item_index}".encode("utf-8"), qos=2))
                    if reason in (0x00, 0x10):
                        hold_completed += 1
                        completed_round += 1
                peak_pending = max(peak_pending, sent_round - completed_round)
                delivered = _drain_until(subscriber, hold_rate, timeout_seconds=max(2.0, hold_seconds / hold_rounds))
                hold_delivered += delivered
                if (round_index + 1) in hold_checkpoints:
                    ratio_complete = (hold_completed / hold_sent) if hold_sent > 0 else 0.0
                    ratio_delivered = (hold_delivered / hold_sent) if hold_sent > 0 else 0.0
                    hold_elapsed_now = max(1e-6, time.monotonic() - hold_started)
                    actual_rate_now = float(hold_delivered) / hold_elapsed_now
                    step(
                        f"hold {(round_index + 1)}/{hold_rounds} completed={hold_completed}/{hold_sent} "
                        f"delivered={hold_delivered}/{hold_sent} pending_peak={peak_pending}"
                    )
                    step(
                        f"hold ratios complete={ratio_complete:.4f} delivery={ratio_delivered:.4f} "
                        f"hold_elapsed={hold_elapsed_now:.3f}s actual_msgs_per_s={actual_rate_now:.2f}"
                    )
            hold_elapsed = max(1e-6, time.monotonic() - hold_started)

    ratio_complete = (hold_completed / hold_sent) if hold_sent > 0 else 0.0
    ratio_delivered = (hold_delivered / hold_sent) if hold_sent > 0 else 0.0
    success = ratio_complete >= 0.99 and ratio_delivered >= 0.99
    counters = {
        "hold_sent": hold_sent,
        "hold_completed": hold_completed,
        "hold_delivered": hold_delivered,
        "peak_pending": peak_pending,
    }
    summary = (
        f"hold_sent={hold_sent} complete_ratio={ratio_complete:.4f} "
        f"deliver_ratio={ratio_delivered:.4f} peak_pending={peak_pending}"
    )
    step(f"final success={success} complete_ratio={ratio_complete:.4f} delivery_ratio={ratio_delivered:.4f}")
    return ScenarioOutcome(
        success=success,
        summary=summary,
        counters=counters,
        work_units=float(hold_delivered),
        throughput_per_second=_safe_rate(float(hold_delivered), hold_elapsed),
        throughput_basis_seconds=hold_elapsed,
    )



def definition() -> helpers.ScenarioDef:
    scenario_id = "P03"
    spec = next(item for item in helpers.SCENARIO_SPECS if item.scenario_id == scenario_id)
    return helpers.ScenarioDef(spec=spec, execute=execute)
