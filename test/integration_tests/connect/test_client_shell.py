"""Integration tests for local Step 27 test-client shell connect flow."""

from __future__ import annotations

import os
import subprocess
import time
import uuid
from pathlib import Path


def _project_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _find_yahatestclient() -> Path | None:
    binary_name = "yahatestclient.exe" if os.name == "nt" else "yahatestclient"
    candidate = _project_root() / "build" / "release" / binary_name
    if candidate.exists():
        return candidate
    return None


def _build_yahatestclient_if_missing() -> Path | None:
    existing_binary = _find_yahatestclient()
    if existing_binary is not None:
        return existing_binary

    build_command = [
        "cmake",
        "--build",
        "--preset",
        "release",
        "--target",
        "yahatestclient",
    ]
    completed = subprocess.run(
        build_command,
        cwd=_project_root(),
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        return None

    return _find_yahatestclient()


def run_1_9_1_local_test_client_connect(config) -> tuple[bool, str]:
    binary_path = _build_yahatestclient_if_missing()
    if binary_path is None:
        return False, "yahatestclient binary not found and build failed"

    client_id = f"integration-step27-{uuid.uuid4().hex[:10]}"
    command = [
        str(binary_path),
        "connect",
        "--host",
        config.host,
        "--port",
        str(config.port),
        "--transport",
        "mqtt",
        "--client-id",
        client_id,
        "--keep-alive-seconds",
        "10",
        "--reconnect-period-ms",
        "200",
        "--maximum-reconnect-times",
        "0",
    ]

    process = None
    try:
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=_project_root(),
        )

        # If connect fails, process exits quickly; on success it keeps running.
        time.sleep(min(2.0, max(0.5, config.timeout_seconds / 2.0)))

        if process.poll() is not None:
            stdout_text, stderr_text = process.communicate(timeout=1.0)
            merged = "\n".join(
                chunk for chunk in [stdout_text.strip(), stderr_text.strip()] if chunk
            ).strip()
            return False, (
                "local yahatestclient connect exited early with "
                f"code {process.returncode}: {merged or 'no output'}"
            )

        process.terminate()
        try:
            process.wait(timeout=config.timeout_seconds)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2.0)

        if process.returncode not in (0, None):
            stdout_text, stderr_text = process.communicate(timeout=1.0)
            merged = "\n".join(
                chunk for chunk in [stdout_text.strip(), stderr_text.strip()] if chunk
            ).strip()
            return False, (
                "local yahatestclient did not shutdown cleanly after signal: "
                f"exit={process.returncode}, output={merged or 'no output'}"
            )

        return True, "1.9.1 local yahatestclient connect lifecycle succeeded"

    except Exception as error:
        return False, f"1.9.1 failed: {error}"
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=2.0)


TEST_CASES = [
    {
        "name": "connect/test_client_shell_connect",
        "description": "1.9.1 Local yahatestclient connect stays up until signal and exits cleanly",
        "run": run_1_9_1_local_test_client_connect,
    }
]
