"""Scenario P03 definition."""

from __future__ import annotations

from .. import helpers


def definition() -> helpers.ScenarioDef:
    scenario_id = "P03"
    spec = next(item for item in helpers.SCENARIO_SPECS if item.scenario_id == scenario_id)
    return helpers.ScenarioDef(spec=spec, execute=helpers._scenario_p03)
