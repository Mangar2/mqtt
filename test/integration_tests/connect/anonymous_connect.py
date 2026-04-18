"""Integration tests for anonymous connect behavior."""

from __future__ import annotations

import subprocess


def run_connect_anonymous(config) -> tuple[bool, str]:
    command = [
        "mqttx",
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
        return False, "mqttx command not found on PATH"
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
        "description": "Connect to broker without credentials using mqttx",
        "run": run_connect_anonymous,
    }
]
