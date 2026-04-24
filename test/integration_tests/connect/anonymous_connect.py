"""Integration tests for anonymous connect behavior."""

from __future__ import annotations

import subprocess
from pathlib import Path
import shutil


def _find_mqttx() -> str | None:
    """Find mqttx binary: first in local test folder, then in PATH."""
    # Check local test folder
    test_dir = Path(__file__).resolve().parents[2]
    local_mqttx = test_dir / "mqttx.exe"
    if local_mqttx.exists():
        return str(local_mqttx)
    
    # Fall back to PATH
    return shutil.which("mqttx")


def run_connect_anonymous(config) -> tuple[bool, str]:
    mqttx_path = _find_mqttx()
    if not mqttx_path:
        return False, "mqttx command not found (checked local test folder and PATH)"
    
    command = [
        mqttx_path,
        "pub",
        "--hostname",
        config.host,
        "--port",
        str(config.port),
        "--topic",
        "integration/connect/anonymous",
        "--message",
        "connect-test",
        "--qos",
        "0",
        "--reconnect-period",
        "0",
    ]

    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=config.timeout_seconds,
        )
    except FileNotFoundError:
        return False, f"mqttx executable not found at {mqttx_path}"
    except subprocess.TimeoutExpired:
        return False, f"mqttx command timed out after {config.timeout_seconds:.1f}s"

    output = "\n".join(part for part in [completed.stdout.strip(), completed.stderr.strip()] if part).strip()
    if completed.returncode == 0:
        return True, output or "mqttx connected and publish completed"

    failure_reason = output or "mqttx returned non-zero without output"
    return False, f"exit={completed.returncode} {failure_reason}"


TEST_CASES = [
    {
        "name": "connect/anonymous",
        "description": "1.1.9 Connect to broker without credentials using mqttx publish flow",
        "run": run_connect_anonymous,
    }
]
