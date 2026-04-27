"""Thin integration-test module that exposes shared local yahatestclient test cases."""

from __future__ import annotations

import importlib.util
from pathlib import Path


_HELPER_PATH = Path(__file__).resolve().parents[2] / "integration_client_shell_cases.py"

_spec = importlib.util.spec_from_file_location("integration_client_shell_cases", _HELPER_PATH)
if _spec is None or _spec.loader is None:
    raise RuntimeError(f"Unable to load client test helper module: {_HELPER_PATH}")

_module = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_module)

TEST_CASES = list(_module.TEST_CASES)
