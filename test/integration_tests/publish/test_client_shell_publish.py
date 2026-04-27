"""Integration tests for local Step 29 test-client publish flow without subscribers."""

from __future__ import annotations

import os
import subprocess
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


def _build_yahatestclient() -> Path | None:
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


def _run_publish(
    config,
    extra_args: list[str],
    *,
    stdin_text: str | None = None,
) -> tuple[bool, str]:
    binary_path = _build_yahatestclient()
    if binary_path is None:
        return False, "yahatestclient binary not found and build failed"

    topic = f"integration/step29/no-subscribers/{uuid.uuid4().hex}"
    client_id = f"step29-pub-{uuid.uuid4().hex[:10]}"

    command = [
        str(binary_path),
        "publish",
        "--host",
        config.host,
        "--port",
        str(config.port),
        "--transport",
        "mqtt",
        "--client-id",
        client_id,
        "--topic",
        topic,
        "--maximum-reconnect-times",
        "0",
    ]
    command.extend(extra_args)

    try:
        completed = subprocess.run(
            command,
            cwd=_project_root(),
            input=stdin_text,
            capture_output=True,
            text=True,
            timeout=max(2.0, config.timeout_seconds),
            check=False,
        )
    except subprocess.TimeoutExpired:
        return False, "yahatestclient publish command timed out"
    except Exception as error:
        return False, f"yahatestclient publish execution failed: {error}"

    merged = "\n".join(
        chunk for chunk in [completed.stdout.strip(), completed.stderr.strip()] if chunk
    ).strip()
    if completed.returncode != 0:
        return False, (
            "publish command failed: "
            f"exit={completed.returncode}, output={merged or 'no output'}"
        )

    if "Publish succeeded" not in completed.stdout:
        return False, (
            "publish command exited zero but success marker missing: "
            f"output={merged or 'no output'}"
        )

    return True, merged or "publish command succeeded"


def run_test_client_publish_qos0_inline_no_subscribers(config) -> tuple[bool, str]:
    return _run_publish(
        config,
        [
            "--qos",
            "0",
            "--payload",
            "hello-step29-qos0",
            "--payload-encoding",
            "raw",
        ],
    )


def run_test_client_publish_qos1_hex_properties_no_subscribers(config) -> tuple[bool, str]:
    return _run_publish(
        config,
        [
            "--qos",
            "1",
            "--payload",
            "48656c6c6f2053514f5331",
            "--payload-encoding",
            "hex",
            "--payload-format-indicator",
            "1",
            "--message-expiry-interval-seconds",
            "30",
            "--topic-alias",
            "4",
            "--response-topic",
            "integration/step29/response",
            "--correlation-data",
            "dGVzdC1jb3Jy",
            "--correlation-data-encoding",
            "base64",
            "--subscription-identifier",
            "12",
            "--content-type",
            "text/plain",
            "--publish-user-property",
            "k=v",
        ],
    )


def run_test_client_publish_qos2_stdin_multiline_no_subscribers(config) -> tuple[bool, str]:
    return _run_publish(
        config,
        [
            "--qos",
            "2",
            "--payload-stdin-multiline",
            "--payload-encoding",
            "raw",
            "--retain",
            "false",
            "--dup",
            "false",
        ],
        stdin_text="line-one\nline-two\nline-three\n",
    )


TEST_CASES = [
    {
        "name": "publish/test_client_shell_publish_qos0_no_subscribers",
        "description": "Step29 test client publish QoS0 without subscribers succeeds",
        "run": run_test_client_publish_qos0_inline_no_subscribers,
    },
    {
        "name": "publish/test_client_shell_publish_qos1_properties_no_subscribers",
        "description": "Step29 test client publish QoS1 with MQTT5 properties without subscribers succeeds",
        "run": run_test_client_publish_qos1_hex_properties_no_subscribers,
    },
    {
        "name": "publish/test_client_shell_publish_qos2_stdin_no_subscribers",
        "description": "Step29 test client publish QoS2 from multiline stdin without subscribers succeeds",
        "run": run_test_client_publish_qos2_stdin_multiline_no_subscribers,
    },
]
