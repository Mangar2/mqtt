"""Shared integration test cases for local yahatestclient connect/publish/subscribe flows."""

from __future__ import annotations

import json
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


def _run_cli_command(args: list[str], timeout_seconds: float) -> tuple[int, str, str]:
    binary_path = _build_yahatestclient()
    if binary_path is None:
        return 127, "", "yahatestclient binary not found and build failed"

    command = [str(binary_path)]
    command.extend(args)
    completed = subprocess.run(
        command,
        cwd=_project_root(),
        capture_output=True,
        text=True,
        timeout=timeout_seconds,
        check=False,
    )
    return completed.returncode, completed.stdout, completed.stderr


def _extract_metrics_json(output_text: str) -> tuple[bool, str]:
    for line in output_text.splitlines():
        if not line.startswith("LOAD_METRICS_JSON "):
            continue
        raw_json = line[len("LOAD_METRICS_JSON ") :].strip()
        try:
            parsed = json.loads(raw_json)
        except json.JSONDecodeError as error:
            return False, f"invalid LOAD_METRICS_JSON payload: {error}"

        required_keys = {
            "mode",
            "attempted",
            "succeeded",
            "failed",
            "timed_out",
            "duration_ms",
            "throughput_ops_per_sec",
            "latency_avg_ms",
            "latency_min_ms",
            "latency_max_ms",
        }
        missing = sorted(required_keys - set(parsed.keys()))
        if missing:
            return False, f"LOAD_METRICS_JSON missing keys: {', '.join(missing)}"

        return True, "LOAD_METRICS_JSON parsed successfully"

    return False, "LOAD_METRICS_JSON line missing in scenario output"


def _run_scenario_load_mode(
    config,
    *,
    mode: str,
    connection_count: int,
    publish_limit: int,
    connect_interval_ms: int = 0,
    message_interval_ms: int = 0,
) -> tuple[bool, str]:
    binary_path = _build_yahatestclient()
    if binary_path is None:
        return False, "yahatestclient binary not found and build failed"

    unique = uuid.uuid4().hex[:8]
    command = [
        str(binary_path),
        "scenario",
        "--host",
        config.host,
        "--port",
        str(config.port),
        "--transport",
        "mqtt",
        "--client-id",
        f"step32-int-{unique}",
        "--maximum-reconnect-times",
        "0",
        "--load-mode",
        mode,
        "--connection-count",
        str(connection_count),
        "--connect-interval-ms",
        str(connect_interval_ms),
        "--message-interval-ms",
        str(message_interval_ms),
        "--publish-limit",
        str(publish_limit),
        "--topic-template",
        f"integration/step32/{mode}/{unique}/{{index}}",
        "--client-template",
        f"integration-step32-{mode}-{unique}-{{index}}",
        "--metrics-json",
    ]

    timeout_seconds = max(6.0, config.timeout_seconds * 3.0)
    if mode == "multi-subscribe":
        timeout_seconds = max(timeout_seconds, 25.0)

    try:
        completed = subprocess.run(
            command,
            cwd=_project_root(),
            capture_output=True,
            text=True,
            timeout=timeout_seconds,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return False, f"scenario load mode {mode} timed out"
    except Exception as error:  # pylint: disable=broad-except
        return False, f"scenario load mode {mode} execution failed: {error}"

    merged = "\n".join(
        chunk for chunk in [completed.stdout.strip(), completed.stderr.strip()] if chunk
    ).strip()
    if completed.returncode != 0:
        return False, (
            f"scenario load mode {mode} failed: "
            f"exit={completed.returncode}, output={merged or 'no output'}"
        )

    parse_ok, parse_details = _extract_metrics_json(completed.stdout)
    if not parse_ok:
        return False, f"scenario load mode {mode} missing valid metrics: {parse_details}"

    return True, merged or f"scenario load mode {mode} succeeded"


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


def run_test_client_shell_scenario_mass_connect_metrics_json(config) -> tuple[bool, str]:
    return _run_scenario_load_mode(
        config,
        mode="mass-connect",
        connection_count=3,
        publish_limit=3,
    )


def run_test_client_shell_scenario_publish_rate_metrics_json(config) -> tuple[bool, str]:
    return _run_scenario_load_mode(
        config,
        mode="publish-rate",
        connection_count=2,
        publish_limit=6,
        message_interval_ms=5,
    )


def run_test_client_shell_scenario_list_includes_step32_modes(config) -> tuple[bool, str]:
    binary_path = _build_yahatestclient()
    if binary_path is None:
        return False, "yahatestclient binary not found and build failed"

    command = [
        str(binary_path),
        "scenario",
        "--host",
        config.host,
        "--port",
        str(config.port),
        "--transport",
        "mqtt",
        "--list-scenarios",
    ]

    try:
        completed = subprocess.run(
            command,
            cwd=_project_root(),
            capture_output=True,
            text=True,
            timeout=max(6.0, config.timeout_seconds * 2.0),
            check=False,
        )
    except subprocess.TimeoutExpired:
        return False, "scenario --list-scenarios timed out"
    except Exception as error:  # pylint: disable=broad-except
        return False, f"scenario --list-scenarios execution failed: {error}"

    merged = "\n".join(
        chunk for chunk in [completed.stdout.strip(), completed.stderr.strip()] if chunk
    ).strip()
    if completed.returncode != 0:
        return False, (
            "scenario --list-scenarios failed: "
            f"exit={completed.returncode}, output={merged or 'no output'}"
        )

    required_tokens = ["mass-connect", "publish-rate", "multi-subscribe"]
    missing = [token for token in required_tokens if token not in completed.stdout]
    if missing:
        return False, (
            "scenario catalog missing Step32 load modes: " + ", ".join(missing)
        )

    return True, merged or "scenario catalog includes Step32 load modes"


def run_test_client_shell_wp1_command_help_discoverability(config) -> tuple[bool, str]:
    help_invocations: list[tuple[str, list[str], list[str]]] = [
        ("top-level help", ["--help"], ["Usage:", "Commands:"]),
        ("bench help", ["bench", "--help"], ["bench", "conn", "pub", "sub"]),
        ("bench conn help", ["bench", "conn", "--help"], ["bench", "conn"]),
        ("bench pub help", ["bench", "pub", "--help"], ["bench", "pub"]),
        ("bench sub help", ["bench", "sub", "--help"], ["bench", "sub"]),
        ("conn help", ["conn", "--help"], ["conn", "help-only"]),
        ("sub help", ["sub", "--help"], ["subscribe", "sub"]),
        ("simulate help", ["simulate", "--help"], ["simulate", "help-only"]),
        ("ls help", ["ls", "--help"], ["ls", "help-only"]),
        ("init help", ["init", "--help"], ["init", "help-only"]),
        ("check help", ["check", "--help"], ["check", "help-only"]),
    ]

    timeout_seconds = max(4.0, config.timeout_seconds)
    for label, argv, required_tokens in help_invocations:
        try:
            returncode, stdout_text, stderr_text = _run_cli_command(argv, timeout_seconds)
        except subprocess.TimeoutExpired:
            return False, f"{label} timed out"
        except Exception as error:  # pylint: disable=broad-except
            return False, f"{label} execution failed: {error}"

        if returncode != 0:
            merged = "\n".join(
                chunk for chunk in [stdout_text.strip(), stderr_text.strip()] if chunk
            ).strip()
            return False, (
                f"{label} failed: exit={returncode}, "
                f"output={merged or 'no output'}"
            )

        for token in required_tokens:
            if token not in stdout_text:
                return False, f"{label} output missing token: {token}"

    return True, "WP1 command help discoverability checks succeeded"


def run_test_client_shell_wp1_version_output_contract(config) -> tuple[bool, str]:
    timeout_seconds = max(4.0, config.timeout_seconds)
    for argv in (["--version"], ["-v"]):
        try:
            returncode, stdout_text, stderr_text = _run_cli_command(argv, timeout_seconds)
        except subprocess.TimeoutExpired:
            return False, f"version invocation {' '.join(argv)} timed out"
        except Exception as error:  # pylint: disable=broad-except
            return False, f"version invocation {' '.join(argv)} failed: {error}"

        if returncode != 0:
            merged = "\n".join(
                chunk for chunk in [stdout_text.strip(), stderr_text.strip()] if chunk
            ).strip()
            return False, (
                f"version invocation {' '.join(argv)} failed: "
                f"exit={returncode}, output={merged or 'no output'}"
            )

        if not stdout_text.startswith("yahatestclient "):
            return False, (
                f"version output contract broken for {' '.join(argv)}: "
                f"stdout={stdout_text.strip()!r}"
            )

    return True, "WP1 version output contract checks succeeded"


def run_test_client_shell_wp1_unknown_behavior_parity(config) -> tuple[bool, str]:
    timeout_seconds = max(4.0, config.timeout_seconds)

    try:
        unknown_command_code, unknown_command_stdout, unknown_command_stderr = _run_cli_command(
            ["invalid-command"], timeout_seconds
        )
    except subprocess.TimeoutExpired:
        return False, "unknown-command invocation timed out"
    except Exception as error:  # pylint: disable=broad-except
        return False, f"unknown-command invocation failed: {error}"

    if unknown_command_code == 0:
        return False, "invalid command unexpectedly returned success"
    unknown_command_text = "\n".join(
        chunk
        for chunk in [unknown_command_stdout.strip(), unknown_command_stderr.strip()]
        if chunk
    )
    if "Unknown command:" not in unknown_command_text:
        return False, "invalid command did not report Unknown command"

    try:
        unknown_option_code, unknown_option_stdout, unknown_option_stderr = _run_cli_command(
            ["pub", "--unknown"], timeout_seconds
        )
    except subprocess.TimeoutExpired:
        return False, "unknown-option invocation timed out"
    except Exception as error:  # pylint: disable=broad-except
        return False, f"unknown-option invocation failed: {error}"

    if unknown_option_code == 0:
        return False, "invalid option unexpectedly returned success"
    unknown_option_text = "\n".join(
        chunk
        for chunk in [unknown_option_stdout.strip(), unknown_option_stderr.strip()]
        if chunk
    )
    if "Unknown option:" not in unknown_option_text:
        return False, "invalid option did not report Unknown option"

    return True, "WP1 unknown command/option behavior checks succeeded"


def _run_reconnect_failure_case(
    argv: list[str],
    timeout_seconds: float,
) -> tuple[bool, float, str]:
    started_at = time.monotonic()
    returncode, stdout_text, stderr_text = _run_cli_command(argv, timeout_seconds)
    elapsed_seconds = time.monotonic() - started_at

    merged = "\n".join(
        chunk for chunk in [stdout_text.strip(), stderr_text.strip()] if chunk
    ).strip()
    if returncode == 0:
        return False, elapsed_seconds, "command unexpectedly succeeded"
    return True, elapsed_seconds, merged or "failed as expected"


def run_test_client_shell_wp2_pub_reconnect_matrix(config) -> tuple[bool, str]:
    unreachable_port = "1"
    timeout_seconds = max(8.0, config.timeout_seconds)

    ok_zero, _, detail_zero = _run_reconnect_failure_case(
        [
            "pub",
            "-t",
            "integration/wp2/reconnect",
            "-m",
            "payload",
            "-h",
            "127.0.0.1",
            "-p",
            unreachable_port,
            "-rp",
            "200",
            "--maximum-reconnect-times",
            "0",
        ],
        timeout_seconds,
    )
    if not ok_zero:
        return False, f"max=0 case failed: {detail_zero}"

    ok_two, _, detail_two = _run_reconnect_failure_case(
        [
            "pub",
            "-t",
            "integration/wp2/reconnect",
            "-m",
            "payload",
            "-h",
            "127.0.0.1",
            "-p",
            unreachable_port,
            "-rp",
            "200",
            "--maximum-reconnect-times",
            "2",
        ],
        timeout_seconds,
    )
    if not ok_two:
        return False, f"max=2 case failed: {detail_two}"

    if "Publish attempt 1 failed" not in detail_zero:
        return False, "max=0 case missing attempt-1 failure trace"
    if "Publish attempt 2 failed" in detail_zero:
        return False, "max=0 case executed unexpected second attempt"
    if "Publish attempt 3 failed" in detail_zero:
        return False, "max=0 case executed unexpected third attempt"

    if "Publish attempt 1 failed" not in detail_two:
        return False, "max=2 case missing attempt-1 failure trace"
    if "Publish attempt 2 failed" not in detail_two:
        return False, "max=2 case missing attempt-2 failure trace"
    if "Publish attempt 3 failed" not in detail_two:
        return False, "max=2 case missing attempt-3 failure trace"

    return True, "WP2 pub reconnect matrix checks succeeded"


def run_test_client_shell_wp2_bench_reconnect_matrix(config) -> tuple[bool, str]:
    timeout_seconds = max(8.0, config.timeout_seconds)
    ok, _, detail = _run_reconnect_failure_case(
        [
            "bench",
            "pub",
            "-t",
            "integration/wp2/bench/%i",
            "-m",
            "payload",
            "-h",
            "127.0.0.1",
            "-p",
            "1",
            "-rp",
            "200",
            "--maximum-reconnect-times",
            "2",
            "-c",
            "1",
            "-L",
            "1",
        ],
        timeout_seconds,
    )
    if not ok:
        return False, detail

    if "Step32 publish attempt 1 failed" not in detail:
        return False, "bench case missing Step32 publish attempt-1 trace"
    if "Step32 publish attempt 2 failed" not in detail:
        return False, "bench case missing Step32 publish attempt-2 trace"
    if "Step32 publish attempt 3 failed" not in detail:
        return False, "bench case missing Step32 publish attempt-3 trace"

    return True, "WP2 bench reconnect matrix check succeeded"


def run_test_client_shell_wp2_reconnect_alias_precedence(config) -> tuple[bool, str]:
    timeout_seconds = max(8.0, config.timeout_seconds)

    ok_alias_last, _, detail_alias_last = _run_reconnect_failure_case(
        [
            "pub",
            "-t",
            "integration/wp2/alias",
            "-m",
            "payload",
            "-h",
            "127.0.0.1",
            "-p",
            "1",
            "-rp",
            "200",
            "--maximum-reconnect-times",
            "0",
            "--maximun-reconnect-times",
            "2",
        ],
        timeout_seconds,
    )
    if not ok_alias_last:
        return False, f"alias-last case failed: {detail_alias_last}"

    ok_standard_last, _, detail_standard_last = _run_reconnect_failure_case(
        [
            "pub",
            "-t",
            "integration/wp2/alias",
            "-m",
            "payload",
            "-h",
            "127.0.0.1",
            "-p",
            "1",
            "-rp",
            "200",
            "--maximun-reconnect-times",
            "2",
            "--maximum-reconnect-times",
            "0",
        ],
        timeout_seconds,
    )
    if not ok_standard_last:
        return False, f"standard-last case failed: {detail_standard_last}"

    if "Publish attempt 3 failed" not in detail_alias_last:
        return False, "alias-last case did not apply max reconnect alias value"
    if "Publish attempt 2 failed" in detail_standard_last:
        return False, "standard-last case did not preserve last-option precedence"

    return True, "WP2 reconnect alias and precedence checks succeeded"


def run_test_client_shell_wp3_bench_persistent_connections_and_split(config) -> tuple[bool, str]:
    timeout_seconds = max(8.0, config.timeout_seconds * 2.0)
    unique = uuid.uuid4().hex
    returncode, stdout_text, stderr_text = _run_cli_command(
        [
            "bench",
            "pub",
            "-h",
            config.host,
            "-p",
            str(config.port),
            "-t",
            f"integration/wp3/persistent/{unique}/%i",
            "-m",
            "one|two|three",
            "--split",
            "|",
            "-c",
            "2",
            "-i",
            "2",
            "-im",
            "1",
            "-L",
            "3",
            "-v",
            "--maximum-reconnect-times",
            "0",
        ],
        timeout_seconds,
    )

    merged = "\n".join(
        chunk for chunk in [stdout_text.strip(), stderr_text.strip()] if chunk
    ).strip()
    if returncode != 0:
        return False, (
            "wp3 persistent/split bench run failed: "
            f"exit={returncode}, output={merged or 'no output'}"
        )

    if "WP3 persistent bench connections established=2" not in stdout_text:
        return False, "missing persistent connection establishment trace"
    if "attempted=3 succeeded=3" not in stdout_text:
        return False, "unexpected summary counters for finite limit"
    if "payload='one'" not in stdout_text:
        return False, "split payload token 'one' missing"
    if "payload='two'" not in stdout_text:
        return False, "split payload token 'two' missing"
    if "payload='three'" not in stdout_text:
        return False, "split payload token 'three' missing"

    return True, "WP3 persistent connection and split payload checks succeeded"


def run_test_client_shell_wp3_bench_payload_size_semantics(config) -> tuple[bool, str]:
    timeout_seconds = max(8.0, config.timeout_seconds * 2.0)
    unique = uuid.uuid4().hex
    returncode, stdout_text, stderr_text = _run_cli_command(
        [
            "bench",
            "pub",
            "-h",
            config.host,
            "-p",
            str(config.port),
            "-t",
            f"integration/wp3/payload-size/{unique}/%i",
            "-m",
            "alpha|beta",
            "--split",
            "|",
            "-S",
            "5",
            "-c",
            "1",
            "-L",
            "2",
            "-v",
            "--maximum-reconnect-times",
            "0",
        ],
        timeout_seconds,
    )

    merged = "\n".join(
        chunk for chunk in [stdout_text.strip(), stderr_text.strip()] if chunk
    ).strip()
    if returncode != 0:
        return False, (
            "wp3 payload-size bench run failed: "
            f"exit={returncode}, output={merged or 'no output'}"
        )

    if "payload_size=5" not in stdout_text:
        return False, "payload-size trace missing"
    if "payload='xxxxx'" not in stdout_text:
        return False, "payload-size generation trace missing expected payload"

    return True, "WP3 payload-size semantics checks succeeded"


def run_test_client_shell_wp3_bench_limit_zero_unlimited(config) -> tuple[bool, str]:
    binary_path = _build_yahatestclient()
    if binary_path is None:
        return False, "yahatestclient binary not found and build failed"

    unique = uuid.uuid4().hex
    command = [
        str(binary_path),
        "bench",
        "pub",
        "-h",
        config.host,
        "-p",
        str(config.port),
        "-t",
        f"integration/wp3/unlimited/{unique}/%i",
        "-m",
        "payload",
        "-c",
        "1",
        "-im",
        "1",
        "-L",
        "0",
        "--maximum-reconnect-times",
        "0",
    ]

    try:
        subprocess.run(
            command,
            cwd=_project_root(),
            capture_output=True,
            text=True,
            timeout=max(1.5, config.timeout_seconds / 2.0),
            check=False,
        )
        return False, "bench pub with --limit 0 exited unexpectedly (expected unlimited run)"
    except subprocess.TimeoutExpired:
        return True, "WP3 limit=0 unlimited-run semantics check succeeded"
    except Exception as error:  # pylint: disable=broad-except
        return False, f"wp3 limit=0 check failed: {error}"


def run_test_client_shell_wp4_pub_payload_size_and_protobuf_schema(config) -> tuple[bool, str]:
    binary_path = _build_yahatestclient()
    if binary_path is None:
        return False, "yahatestclient binary not found and build failed"

    unique = uuid.uuid4().hex
    topic = f"integration/wp4/pub-size/{unique}"
    schema_path = _project_root() / "test" / "integration_tests" / "tmp" / f"schema-{unique}.proto"
    schema_path.parent.mkdir(parents=True, exist_ok=True)
    schema_path.write_text('syntax = "proto3"; message Envelope { string value = 1; }\n', encoding="utf-8")

    subscriber_command = [
        str(binary_path),
        "subscribe",
        "--host",
        config.host,
        "--port",
        str(config.port),
        "--transport",
        "mqtt",
        "--client-id",
        f"wp4-sub-{unique[:10]}",
        "--subscription",
        f"{topic}|0|false|false|0",
        "--clean-output",
        "--message-limit",
        "1",
        "--wait-timeout-ms",
        "6000",
        "--maximum-reconnect-times",
        "0",
    ]

    subscriber_process = None
    try:
        subscriber_process = subprocess.Popen(
            subscriber_command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=_project_root(),
        )

        time.sleep(1.0)

        publish_command = [
            str(binary_path),
            "pub",
            "-h",
            config.host,
            "-p",
            str(config.port),
            "-t",
            topic,
            "-q",
            "0",
            "-f",
            "protobuf",
            "-Pp",
            str(schema_path),
            "-Pmn",
            "Envelope",
            "-S",
            "24",
            "--maximum-reconnect-times",
            "0",
        ]
        publish_completed = subprocess.run(
            publish_command,
            cwd=_project_root(),
            capture_output=True,
            text=True,
            timeout=max(8.0, config.timeout_seconds * 2.0),
            check=False,
        )

        publish_merged = "\n".join(
            chunk
            for chunk in [publish_completed.stdout.strip(), publish_completed.stderr.strip()]
            if chunk
        ).strip()
        if publish_completed.returncode != 0:
            return False, (
                "wp4 pub payload-size/protobuf run failed: "
                f"exit={publish_completed.returncode}, output={publish_merged or 'no output'}"
            )

        try:
            subscriber_stdout, subscriber_stderr = subscriber_process.communicate(timeout=10.0)
        except subprocess.TimeoutExpired:
            subscriber_process.kill()
            subscriber_stdout, subscriber_stderr = subscriber_process.communicate(timeout=2.0)
            return False, "wp4 subscriber timed out waiting for payload"

        merged = "\n".join(
            chunk for chunk in [subscriber_stdout.strip(), subscriber_stderr.strip()] if chunk
        ).strip()
        if subscriber_process.returncode != 0:
            return False, (
                "wp4 subscriber failed: "
                f"exit={subscriber_process.returncode}, output={merged or 'no output'}"
            )

        payload_text = subscriber_stdout.strip()
        if len(payload_text) != 24:
            return False, (
                "wp4 payload-size semantics failed: "
                f"expected 24 bytes, got {len(payload_text)}"
            )

        return True, "WP4 pub payload-size and protobuf schema option checks succeeded"
    except Exception as error:  # pylint: disable=broad-except
        return False, f"wp4 pub payload-size/protobuf check failed: {error}"
    finally:
        if subscriber_process is not None and subscriber_process.poll() is None:
            subscriber_process.kill()
            subscriber_process.wait(timeout=2.0)


def run_test_client_shell_wp4_bench_publish_properties_semantics(config) -> tuple[bool, str]:
    timeout_seconds = max(10.0, config.timeout_seconds * 2.0)
    unique = uuid.uuid4().hex
    returncode, stdout_text, stderr_text = _run_cli_command(
        [
            "bench",
            "pub",
            "-h",
            config.host,
            "-p",
            str(config.port),
            "-t",
            f"integration/wp4/bench/{unique}/%i",
            "-m",
            "payload",
            "-q",
            "1",
            "-d",
            "-pf",
            "1",
            "-e",
            "12",
            "-ta",
            "7",
            "-rt",
            "integration/wp4/reply",
            "-cd",
            "abcd",
            "-si",
            "9",
            "-ct",
            "text/plain",
            "-up",
            "k=v",
            "-c",
            "1",
            "-L",
            "2",
            "-v",
            "--maximum-reconnect-times",
            "0",
        ],
        timeout_seconds,
    )

    merged = "\n".join(
        chunk for chunk in [stdout_text.strip(), stderr_text.strip()] if chunk
    ).strip()
    if returncode != 0:
        return False, (
            "wp4 bench publish properties run failed: "
            f"exit={returncode}, output={merged or 'no output'}"
        )

    required_tokens = [
        "dup=true",
        "pf=true",
        "expiry=true",
        "alias=true",
        "rt=true",
        "cd=true",
        "si=true",
        "ct=true",
        "user_properties=1",
        "attempted=2 succeeded=2",
    ]
    for token in required_tokens:
        if token not in stdout_text:
            return False, f"wp4 bench property trace missing token: {token}"

    return True, "WP4 bench publish property semantics checks succeeded"


def run_test_client_shell_wp5_sub_command_output_pipeline(config) -> tuple[bool, str]:
    binary_path = _build_yahatestclient()
    if binary_path is None:
        return False, "yahatestclient binary not found and build failed"

    unique = uuid.uuid4().hex
    topic = f"integration/wp5/sub/{unique}"
    write_file_path = _project_root() / "test" / "integration_tests" / "tmp" / f"sub-write-{unique}.log"
    save_dir_path = _project_root() / "test" / "integration_tests" / "tmp" / f"sub-save-{unique}"
    write_file_path.parent.mkdir(parents=True, exist_ok=True)

    sub_command = [
        str(binary_path),
        "sub",
        "-h",
        config.host,
        "-p",
        str(config.port),
        "-i",
        f"wp5-sub-{unique[:10]}",
        "-t",
        topic,
        "-q",
        "0",
        "--output-mode",
        "clean",
        "--file-write",
        str(write_file_path),
        "--file-save",
        str(save_dir_path),
        "--delimiter",
        "|",
        "-f",
        "hex",
        "--message-limit",
        "1",
        "--wait-timeout-ms",
        "6000",
        "--maximum-reconnect-times",
        "0",
    ]

    subscriber_process = None
    try:
        subscriber_process = subprocess.Popen(
            sub_command,
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
                "0",
                "--payload",
                "AB",
                "--payload-encoding",
                "raw",
            ],
            topic_override=topic,
        )
        if not publish_ok:
            return False, f"wp5 sub publish side failed: {publish_details}"

        try:
            stdout_text, stderr_text = subscriber_process.communicate(timeout=10.0)
        except subprocess.TimeoutExpired:
            subscriber_process.kill()
            stdout_text, stderr_text = subscriber_process.communicate(timeout=2.0)
            return False, "wp5 sub command timed out before receiving message"

        merged = "\n".join(
            chunk for chunk in [stdout_text.strip(), stderr_text.strip()] if chunk
        ).strip()
        if subscriber_process.returncode != 0:
            return False, (
                "wp5 sub command failed: "
                f"exit={subscriber_process.returncode}, output={merged or 'no output'}"
            )

        if "4142|" not in stdout_text:
            return False, "wp5 sub stdout missing expected formatted payload+delimiter"

        if not write_file_path.exists():
            return False, "wp5 sub file-write output missing"
        write_content = write_file_path.read_text(encoding="utf-8")
        if "4142|" not in write_content:
            return False, "wp5 sub file-write output missing formatted payload"

        if not save_dir_path.exists():
            return False, "wp5 sub file-save directory missing"
        saved_files = sorted(save_dir_path.glob("message-*.txt"))
        if len(saved_files) != 1:
            return False, f"wp5 sub file-save expected 1 file, found {len(saved_files)}"
        saved_content = saved_files[0].read_text(encoding="utf-8")
        if "4142" not in saved_content:
            return False, "wp5 sub file-save content missing formatted payload"

        return True, "WP5 sub command output pipeline checks succeeded"
    except Exception as error:  # pylint: disable=broad-except
        return False, f"wp5 sub output pipeline check failed: {error}"
    finally:
        if subscriber_process is not None and subscriber_process.poll() is None:
            subscriber_process.kill()
            subscriber_process.wait(timeout=2.0)


def run_test_client_shell_wp5_bench_sub_semantics(config) -> tuple[bool, str]:
    timeout_seconds = max(10.0, config.timeout_seconds * 2.0)
    attempts = 3
    returncode = 1
    stdout_text = ""
    stderr_text = ""
    last_failure = ""

    for _ in range(attempts):
        unique = uuid.uuid4().hex
        returncode, stdout_text, stderr_text = _run_cli_command(
            [
                "bench",
                "sub",
                "-h",
                config.host,
                "-p",
                str(config.port),
                "-t",
                f"integration/wp5/bench-sub/{unique}/%i",
                "-q",
                "1",
                "-nl",
                "true",
                "-rap",
                "true",
                "-rh",
                "1",
                "-si",
                "13",
                "-c",
                "1",
                "-L",
                "1",
                "-v",
                "--maximum-reconnect-times",
                "0",
            ],
            timeout_seconds,
        )

        if returncode == 0:
            break

        merged_failure = "\n".join(
            chunk for chunk in [stdout_text.strip(), stderr_text.strip()] if chunk
        ).strip()
        last_failure = merged_failure or "no output"

    merged = "\n".join(
        chunk for chunk in [stdout_text.strip(), stderr_text.strip()] if chunk
    ).strip()
    if returncode != 0:
        return False, (
            "wp5 bench sub run failed: "
            f"exit={returncode}, output={merged or 'no output'}, retries={attempts}, "
            f"last_failure={last_failure}"
        )

    required_tokens = [
        "WP5 bench-sub settings",
        "qos=1",
        "no_local=true",
        "retain_as_published=true",
        "retain_handling=1",
        "subscription_identifier=13",
    ]
    for token in required_tokens:
        if token not in stdout_text:
            return False, f"wp5 bench-sub trace missing token: {token}"

    return True, "WP5 bench sub semantic checks succeeded"


def run_test_client_shell_wp6_simulate_load_mode_alias(config) -> tuple[bool, str]:
    timeout_seconds = max(10.0, config.timeout_seconds * 2.0)
    unique = uuid.uuid4().hex
    returncode, stdout_text, stderr_text = _run_cli_command(
        [
            "simulate",
            "-sc",
            "mass-connect",
            "-h",
            config.host,
            "-p",
            str(config.port),
            "-t",
            f"integration/wp6/simulate/{unique}/%i",
            "-I",
            f"integration-wp6-simulate-{unique}-%i",
            "-c",
            "2",
            "-i",
            "1",
            "-L",
            "2",
            "--metrics-json",
            "--maximum-reconnect-times",
            "0",
        ],
        timeout_seconds,
    )

    merged = "\n".join(
        chunk for chunk in [stdout_text.strip(), stderr_text.strip()] if chunk
    ).strip()
    if returncode != 0:
        return False, (
            "wp6 simulate load-mode alias run failed: "
            f"exit={returncode}, output={merged or 'no output'}"
        )

    if "Step32 load mode: mass-connect" not in stdout_text:
        return False, "wp6 simulate output missing mass-connect load-mode summary"

    json_ok, json_detail = _extract_metrics_json(stdout_text)
    if not json_ok:
        return False, f"wp6 simulate output missing valid metrics json: {json_detail}"

    return True, "WP6 simulate load-mode alias checks succeeded"


def run_test_client_shell_wp6_ls_init_check_command_family(config) -> tuple[bool, str]:
    timeout_seconds = max(8.0, config.timeout_seconds)

    ls_returncode, ls_stdout, ls_stderr = _run_cli_command(
        ["ls", "--scenarios"], timeout_seconds
    )
    if ls_returncode != 0:
        merged = "\n".join(
            chunk for chunk in [ls_stdout.strip(), ls_stderr.strip()] if chunk
        ).strip()
        return False, (
            "wp6 ls --scenarios failed: "
            f"exit={ls_returncode}, output={merged or 'no output'}"
        )
    if "Step32 load modes:" not in ls_stdout:
        return False, "wp6 ls --scenarios output missing load-mode section"

    init_path = _project_root() / "test" / "integration_tests" / "tmp" / f"wp6-init-{uuid.uuid4().hex}.ini"
    init_returncode, init_stdout, init_stderr = _run_cli_command(
        ["init", "--output", str(init_path)], timeout_seconds
    )
    if init_returncode != 0:
        merged = "\n".join(
            chunk for chunk in [init_stdout.strip(), init_stderr.strip()] if chunk
        ).strip()
        return False, (
            "wp6 init failed: "
            f"exit={init_returncode}, output={merged or 'no output'}"
        )
    if not init_path.exists():
        return False, "wp6 init did not create output profile file"
    init_content = init_path.read_text(encoding="utf-8")
    if "host=" not in init_content or "client_id=" not in init_content:
        return False, "wp6 init output profile missing required keys"

    check_returncode, check_stdout, check_stderr = _run_cli_command(
        ["check"], timeout_seconds
    )
    if check_returncode != 0:
        merged = "\n".join(
            chunk for chunk in [check_stdout.strip(), check_stderr.strip()] if chunk
        ).strip()
        return False, (
            "wp6 check failed: "
            f"exit={check_returncode}, output={merged or 'no output'}"
        )

    required_tokens = [
        "status=ok",
        "mqtt_version=5.0",
        "transports=mqtt,ws",
        "tls=unsupported",
    ]
    for token in required_tokens:
        if token not in check_stdout:
            return False, f"wp6 check output missing token: {token}"

    return True, "WP6 ls/init/check command-family checks succeeded"


TEST_CASES = [
    {
        "name": "test-client-shell/test_client_shell_wp1_command_help_discoverability",
        "description": "21.0.1 Local yahatestclient command discoverability help flows succeed for WP1 command surface",
        "run": run_test_client_shell_wp1_command_help_discoverability,
    },
    {
        "name": "test-client-shell/test_client_shell_wp1_version_output_contract",
        "description": "21.0.2 Local yahatestclient version flags produce stable version output contract",
        "run": run_test_client_shell_wp1_version_output_contract,
    },
    {
        "name": "test-client-shell/test_client_shell_wp1_unknown_behavior_parity",
        "description": "21.0.3 Local yahatestclient unknown command and unknown option behavior remains explicit",
        "run": run_test_client_shell_wp1_unknown_behavior_parity,
    },
    {
        "name": "test-client-shell/test_client_shell_wp2_pub_reconnect_matrix",
        "description": "21.0.4 Local yahatestclient pub applies reconnect-period and maximum reconnect attempts on connection failures",
        "run": run_test_client_shell_wp2_pub_reconnect_matrix,
    },
    {
        "name": "test-client-shell/test_client_shell_wp2_bench_reconnect_matrix",
        "description": "21.0.5 Local yahatestclient bench pub applies reconnect-period and maximum reconnect attempts on connection failures",
        "run": run_test_client_shell_wp2_bench_reconnect_matrix,
    },
    {
        "name": "test-client-shell/test_client_shell_wp2_reconnect_alias_precedence",
        "description": "21.0.6 Local yahatestclient accepts mqttx maximun reconnect alias spelling and preserves last-option precedence",
        "run": run_test_client_shell_wp2_reconnect_alias_precedence,
    },
    {
        "name": "test-client-shell/test_client_shell_wp3_bench_persistent_connections_and_split",
        "description": "21.0.7 Local yahatestclient bench pub uses persistent connections and applies split payload semantics",
        "run": run_test_client_shell_wp3_bench_persistent_connections_and_split,
    },
    {
        "name": "test-client-shell/test_client_shell_wp3_bench_payload_size_semantics",
        "description": "21.0.8 Local yahatestclient bench pub applies payload-size generation semantics",
        "run": run_test_client_shell_wp3_bench_payload_size_semantics,
    },
    {
        "name": "test-client-shell/test_client_shell_wp3_bench_limit_zero_unlimited",
        "description": "21.0.9 Local yahatestclient bench pub keeps running for limit zero (unlimited mode)",
        "run": run_test_client_shell_wp3_bench_limit_zero_unlimited,
    },
    {
        "name": "test-client-shell/test_client_shell_wp4_pub_payload_size_and_protobuf_schema",
        "description": "21.0.10 Local yahatestclient pub applies payload-size generation and validates protobuf schema options",
        "run": run_test_client_shell_wp4_pub_payload_size_and_protobuf_schema,
    },
    {
        "name": "test-client-shell/test_client_shell_wp4_bench_publish_properties_semantics",
        "description": "21.0.11 Local yahatestclient bench pub applies publish-property flags in runtime semantics",
        "run": run_test_client_shell_wp4_bench_publish_properties_semantics,
    },
    {
        "name": "test-client-shell/test_client_shell_wp5_sub_command_output_pipeline",
        "description": "21.0.12 Local yahatestclient sub command applies mqttx output pipeline aliases and payload formatting",
        "run": run_test_client_shell_wp5_sub_command_output_pipeline,
    },
    {
        "name": "test-client-shell/test_client_shell_wp5_bench_sub_semantics",
        "description": "21.0.13 Local yahatestclient bench sub applies qos/no-local/retain/subscription-identifier semantics",
        "run": run_test_client_shell_wp5_bench_sub_semantics,
    },
    {
        "name": "test-client-shell/test_client_shell_wp6_simulate_load_mode_alias",
        "description": "21.0.14 Local yahatestclient simulate command maps to step32 load-mode execution and metrics output",
        "run": run_test_client_shell_wp6_simulate_load_mode_alias,
    },
    {
        "name": "test-client-shell/test_client_shell_wp6_ls_init_check_command_family",
        "description": "21.0.15 Local yahatestclient ls/init/check command family executes in-scope runtime behavior",
        "run": run_test_client_shell_wp6_ls_init_check_command_family,
    },
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
    {
        "name": "scenario/test_client_shell_scenario_mass_connect_metrics_json",
        "description": "21.4.1 Local yahatestclient scenario mass-connect succeeds and prints machine-readable metrics",
        "run": run_test_client_shell_scenario_mass_connect_metrics_json,
    },
    {
        "name": "scenario/test_client_shell_scenario_publish_rate_metrics_json",
        "description": "21.4.2 Local yahatestclient scenario publish-rate succeeds and prints machine-readable metrics",
        "run": run_test_client_shell_scenario_publish_rate_metrics_json,
    },
    {
        "name": "scenario/test_client_shell_scenario_list_includes_step32_modes",
        "description": "21.4.3 Local yahatestclient scenario catalog lists all Step32 load modes including multi-subscribe",
        "run": run_test_client_shell_scenario_list_includes_step32_modes,
    },
]
