"""Shared integration test cases for local yahatestclient connect/publish/subscribe flows."""

from __future__ import annotations

import os
import subprocess
import time
import uuid
from pathlib import Path


def _project_root() -> Path:
    return Path(__file__).resolve().parents[1]


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
    topic_override: str | None = None,
) -> tuple[bool, str]:
    binary_path = _build_yahatestclient()
    if binary_path is None:
        return False, "yahatestclient binary not found and build failed"

    topic = topic_override or f"integration/step30/{uuid.uuid4().hex}"
    client_id = f"step30-pub-{uuid.uuid4().hex[:10]}"

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
    except Exception as error:  # pylint: disable=broad-except
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


def run_test_client_shell_connect(config) -> tuple[bool, str]:
    binary_path = _build_yahatestclient()
    if binary_path is None:
        return False, "yahatestclient binary not found and build failed"

    client_id = f"integration-step30-{uuid.uuid4().hex[:10]}"
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

        return True, "local yahatestclient connect lifecycle succeeded"

    except Exception as error:  # pylint: disable=broad-except
        return False, f"connect lifecycle failed: {error}"
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=2.0)


def run_test_client_shell_publish_qos0_no_subscribers(config) -> tuple[bool, str]:
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


def run_test_client_shell_publish_qos1_properties_no_subscribers(config) -> tuple[bool, str]:
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


def run_test_client_shell_publish_qos2_stdin_no_subscribers(config) -> tuple[bool, str]:
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


def _run_subscribe_and_publish_roundtrip(
    config,
    *,
    subscribe_qos: int,
    publish_qos: int,
    use_output_file: bool,
) -> tuple[bool, str]:
    binary_path = _build_yahatestclient()
    if binary_path is None:
        return False, "yahatestclient binary not found and build failed"

    topic = f"integration/step30/roundtrip/{uuid.uuid4().hex}"
    payload = f"step30-message-{uuid.uuid4().hex[:8]}"
    subscriber_client_id = f"step30-sub-{uuid.uuid4().hex[:10]}"

    output_path = _project_root() / "test" / "integration_tests" / "tmp" / f"sub-{uuid.uuid4().hex}.log"
    output_path.parent.mkdir(parents=True, exist_ok=True)

    subscribe_command = [
        str(binary_path),
        "subscribe",
        "--host",
        config.host,
        "--port",
        str(config.port),
        "--transport",
        "mqtt",
        "--client-id",
        subscriber_client_id,
        "--subscription",
        f"{topic}|{subscribe_qos}|false|false|0",
        "--message-limit",
        "1",
        "--wait-timeout-ms",
        "6000",
        "--maximum-reconnect-times",
        "0",
    ]

    if use_output_file:
        subscribe_command.extend([
            "--output-file",
            str(output_path),
            "--append-output",
            "--output-format",
            "{topic}:{payload}",
            "--output-delimiter",
            "\\n",
        ])
    else:
        subscribe_command.append("--clean-output")

    subscriber_process = None
    try:
        subscriber_process = subprocess.Popen(
            subscribe_command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=_project_root(),
        )

        time.sleep(1.0)

        publish_ok, publish_details = _run_publish(
            config,
            [
                "--qos",
                str(publish_qos),
                "--payload",
                payload,
                "--payload-encoding",
                "raw",
            ],
            topic_override=topic,
        )
        if not publish_ok:
            return False, f"publish side failed: {publish_details}"

        try:
            stdout_text, stderr_text = subscriber_process.communicate(timeout=10.0)
        except subprocess.TimeoutExpired:
            subscriber_process.kill()
            stdout_text, stderr_text = subscriber_process.communicate(timeout=2.0)
            return False, "subscribe command timed out before receiving expected message"

        merged_output = "\n".join(
            chunk for chunk in [stdout_text.strip(), stderr_text.strip()] if chunk
        ).strip()
        if subscriber_process.returncode != 0:
            return False, (
                "subscribe command failed: "
                f"exit={subscriber_process.returncode}, output={merged_output or 'no output'}"
            )

        if use_output_file:
            if not output_path.exists():
                return False, "subscribe output file was not created"
            saved_content = output_path.read_text(encoding="utf-8")
            if payload not in saved_content or topic not in saved_content:
                return False, (
                    "subscribe output file missing expected formatted message: "
                    f"content={saved_content!r}"
                )
        else:
            if payload not in stdout_text:
                return False, (
                    "subscribe stdout missing expected payload: "
                    f"output={merged_output or 'no output'}"
                )

        return True, merged_output or "subscribe/publish roundtrip succeeded"

    except Exception as error:  # pylint: disable=broad-except
        return False, f"subscribe/publish roundtrip failed: {error}"
    finally:
        if subscriber_process is not None and subscriber_process.poll() is None:
            subscriber_process.kill()
            subscriber_process.wait(timeout=2.0)


def run_test_client_shell_subscribe_qos0_publish_qos0_roundtrip(config) -> tuple[bool, str]:
    return _run_subscribe_and_publish_roundtrip(
        config,
        subscribe_qos=0,
        publish_qos=0,
        use_output_file=False,
    )


def run_test_client_shell_subscribe_qos1_publish_qos1_output_file_roundtrip(config) -> tuple[bool, str]:
    return _run_subscribe_and_publish_roundtrip(
        config,
        subscribe_qos=1,
        publish_qos=1,
        use_output_file=True,
    )


TEST_CASES = [
    {
        "name": "connect/test_client_shell_connect",
        "description": "21.1.1 Local yahatestclient connect stays up until signal and exits cleanly",
        "run": run_test_client_shell_connect,
    },
    {
        "name": "publish/test_client_shell_publish_qos0_no_subscribers",
        "description": "21.2.1 Local yahatestclient publish QoS0 without subscribers succeeds",
        "run": run_test_client_shell_publish_qos0_no_subscribers,
    },
    {
        "name": "publish/test_client_shell_publish_qos1_properties_no_subscribers",
        "description": "21.2.2 Local yahatestclient publish QoS1 with MQTT5 properties without subscribers succeeds",
        "run": run_test_client_shell_publish_qos1_properties_no_subscribers,
    },
    {
        "name": "publish/test_client_shell_publish_qos2_stdin_no_subscribers",
        "description": "21.2.3 Local yahatestclient publish QoS2 from multiline stdin without subscribers succeeds",
        "run": run_test_client_shell_publish_qos2_stdin_no_subscribers,
    },
    {
        "name": "subscribe/test_client_shell_subscribe_qos0_publish_qos0_roundtrip",
        "description": "21.3.1 Local yahatestclient subscribe receives QoS0 publish via clean output",
        "run": run_test_client_shell_subscribe_qos0_publish_qos0_roundtrip,
    },
    {
        "name": "subscribe/test_client_shell_subscribe_qos1_publish_qos1_output_file_roundtrip",
        "description": "21.3.2 Local yahatestclient subscribe receives QoS1 publish and writes formatted output file",
        "run": run_test_client_shell_subscribe_qos1_publish_qos1_output_file_roundtrip,
    },
]
