"""Scenario definition."""

from __future__ import annotations

from ..helpers import *  # noqa: F401,F403
from .. import helpers

def execute(config: helpers.RunnerConfig) -> helpers.ScenarioOutcome:
    ranges = {
        "small": (10, 100, 10, 0.25, 0.01),
        "middle": (50, 1000, 50, 0.12, 0.003),
        "large": (500, 10000, 500, 0.05, 0.001),
    }
    min_connections, max_connections, step, gap_start, gap_end = ranges[config.size_profile]

    test_duration_seconds = 120.0
    hold_phase_seconds = 30.0
    started = time.monotonic()
    deadline = started + test_duration_seconds

    total_requested = 0
    total_success = 0
    total_rejected = 0
    total_network_close = 0
    total_local_resource = 0
    total_other = 0
    total_closed = 0
    total_wave_runtime = 0.0
    peak_open_connections = 0
    first_overload_target = 0
    first_overload_target_rate = 0.0
    first_overload_achieved_rate = 0.0
    s01_step_index = 1

    def _step(message: str) -> None:
        nonlocal s01_step_index
        print(f"  [S01 STEP {s01_step_index:02d}] {message}")
        s01_step_index += 1

    _step(
        f"profile={config.size_profile} range={min_connections}-{max_connections} "
        f"duration=120s hold=30s"
    )

    # Phase 1: open minimum load and hold for 30s.
    _step(f"phase-1 open baseline {min_connections} parallel connections")
    baseline_counters, baseline_sockets, baseline_duration, baseline_submit_duration = _run_connection_wave(
        config,
        requested_connections=min_connections,
        submit_gap_seconds=gap_start,
        connect_timeout_seconds=max(1.0, min(config.timeout_seconds, 3.0)),
        stop_deadline_monotonic=deadline,
    )
    total_requested += baseline_counters["requested"]
    total_success += baseline_counters["success"]
    total_rejected += baseline_counters["rejected"]
    total_network_close += baseline_counters["network_close"]
    total_local_resource += baseline_counters["local_resource"]
    total_other += baseline_counters["other"]
    total_wave_runtime += baseline_duration
    peak_open_connections = max(peak_open_connections, len(baseline_sockets))
    baseline_offered_rate = baseline_counters["requested"] / baseline_submit_duration
    baseline_accepted_rate = baseline_counters["success"] / baseline_submit_duration
    _step(
        f"phase-1 result success={baseline_counters['success']}/{baseline_counters['requested']} "
        f"offered={baseline_offered_rate:.1f}/s accepted={baseline_accepted_rate:.1f}/s"
    )

    hold_seconds = min(hold_phase_seconds, max(0.0, deadline - time.monotonic()))
    if hold_seconds > 0.0:
        _step(f"phase-1 hold {len(baseline_sockets)} open connections for {hold_seconds:.1f}s")
        time.sleep(hold_seconds)
    total_closed += _close_sockets(baseline_sockets)
    _step(f"phase-1 cleanup closed={total_closed}")

    # Phase 2: progressively increase requested connections and accelerate open rate.
    _step("phase-2 start accelerated ramp")
    ramp_targets = list(range(min_connections + step, max_connections + 1, step))
    if not ramp_targets:
        ramp_targets = [max_connections]

    ramp_window_seconds = max(0.0, test_duration_seconds - hold_phase_seconds)
    _step(f"phase-2 plan waves={len(ramp_targets)} ramp_window={ramp_window_seconds:.1f}s")

    max_target_reached = False
    highest_target_reached = min_connections

    wave_count = len(ramp_targets)
    checkpoint_indices = {
        1,
        max(1, math.ceil(wave_count * 0.25)),
        max(1, math.ceil(wave_count * 0.50)),
        max(1, math.ceil(wave_count * 0.75)),
        wave_count,
    }

    for wave_index, requested_for_round in enumerate(ramp_targets, start=1):
        if time.monotonic() >= deadline:
            break

        remaining_targets = max(1, wave_count - wave_index + 1)
        remaining_time = max(0.0, deadline - time.monotonic())
        slot_seconds = remaining_time / float(remaining_targets)

        # Keep submit and settle phases coupled to the current target and available time.
        submit_budget_seconds = max(0.4, slot_seconds * 0.7)
        submit_gap = max(gap_end, min(gap_start, submit_budget_seconds / float(requested_for_round)))
        target_rate = 1.0 / submit_gap if submit_gap > 0.0 else 0.0

        counters, opened_sockets, wave_duration, submit_duration = _run_connection_wave(
            config,
            requested_connections=requested_for_round,
            submit_gap_seconds=submit_gap,
            connect_timeout_seconds=max(0.8, min(config.timeout_seconds, 2.5)),
            stop_deadline_monotonic=deadline,
        )
        total_requested += counters["requested"]
        total_success += counters["success"]
        total_rejected += counters["rejected"]
        total_network_close += counters["network_close"]
        total_local_resource += counters["local_resource"]
        total_other += counters["other"]
        total_wave_runtime += wave_duration
        peak_open_connections = max(peak_open_connections, len(opened_sockets))
        highest_target_reached = max(highest_target_reached, requested_for_round)
        if requested_for_round >= max_connections:
            max_target_reached = True

        accepted_rate = (counters["success"] / submit_duration) if submit_duration > 0.0 else 0.0
        completion_rate = (counters["success"] / wave_duration) if wave_duration > 0.0 else 0.0
        overloaded = accepted_rate < (target_rate * 0.85)
        if overloaded and first_overload_target == 0:
            first_overload_target = requested_for_round
            first_overload_target_rate = target_rate
            first_overload_achieved_rate = accepted_rate
            _step(
                f"overload detected conn_target={requested_for_round} "
                f"rate_target={target_rate:.1f}/s rate_accepted={accepted_rate:.1f}/s "
                f"threshold={(target_rate * 0.85):.1f}/s"
            )

        if wave_index in checkpoint_indices:
            progress_percent = int((wave_index / float(wave_count)) * 100.0)
            _step(
                f"ramp {progress_percent}% conn_target={requested_for_round} "
                f"success={counters['success']}/{counters['requested']} "
                f"rate_target={target_rate:.1f}/s rate_accepted={accepted_rate:.1f}/s "
                f"rate_complete={completion_rate:.1f}/s"
            )

        remaining_after_wave = max(0.0, deadline - time.monotonic())
        hold_this_round = max(0.0, min(slot_seconds * 0.3, remaining_after_wave))
        if hold_this_round > 0.0:
            time.sleep(hold_this_round)
        total_closed += _close_sockets(opened_sockets)

    _step("phase-2 finished at deadline")

    avg_connect_rate = (total_success / total_wave_runtime) if total_wave_runtime > 0.0 else 0.0
    counters = {
        "range_min": min_connections,
        "range_max": max_connections,
        "requested": total_requested,
        "success": total_success,
        "rejected": total_rejected,
        "network_close": total_network_close,
        "local_resource": total_local_resource,
        "other": total_other,
        "peak_open": peak_open_connections,
        "closed": total_closed,
        "overload_target": first_overload_target,
    }

    overload_text = "none"
    if first_overload_target > 0:
        overload_text = (
            f"target={first_overload_target} requested_rate={first_overload_target_rate:.1f}/s "
            f"achieved_rate={first_overload_achieved_rate:.1f}/s"
        )

    summary = (
        f"profile={config.size_profile} range={min_connections}-{max_connections} duration=120s "
        f"requested={total_requested} success={total_success} fail={total_requested - total_success} "
        f"avg_connect_rate={avg_connect_rate:.2f}/s peak_open={peak_open_connections} "
        f"max_target_reached={max_target_reached} highest_target={highest_target_reached} "
        f"overload={overload_text}"
    )
    _step(
        f"final requested={total_requested} success={total_success} "
        f"avg_rate={avg_connect_rate:.2f}/s peak_open={peak_open_connections} "
        f"highest_target={highest_target_reached}"
    )
    success = total_success > 0 and total_closed == total_success and max_target_reached
    return ScenarioOutcome(success=success, summary=summary, counters=counters, work_units=float(total_success))



def definition() -> helpers.ScenarioDef:
    scenario_id = "S01"
    spec = next(item for item in helpers.SCENARIO_SPECS if item.scenario_id == scenario_id)
    return helpers.ScenarioDef(spec=spec, execute=execute)
