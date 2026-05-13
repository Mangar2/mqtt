"""Integration test: reproduce HTTP publish to ValueService delivery path with real clients."""

from __future__ import annotations

import configparser
import importlib.util
import json
import os
import select
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path
from typing import TextIO


def _load_helper(module_name: str):
    helper_path = Path(__file__).resolve().parents[1] / "helpers" / f"{module_name}.py"
    spec = importlib.util.spec_from_file_location(f"integration_helper_{module_name}", helper_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load helper module {module_name} from {helper_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_broker_module = _load_helper("broker")
_mqtt_client_module = _load_helper("mqtt_client")
_assertions_module = _load_helper("assertions")

start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
resolve_target_host = _broker_module.resolve_target_host
MqttClient = _mqtt_client_module.MqttClient
assert_connack = _assertions_module.assert_connack

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
_RELEASE_DIR = _PROJECT_ROOT / "build" / "release"
_FILESTORE_BINARY = _RELEASE_DIR / ("yahafilestoreclient.exe" if os.name == "nt" else "yahafilestoreclient")
_VALUE_SERVICE_BINARY = _RELEASE_DIR / ("yahavalueserviceclient.exe" if os.name == "nt" else "yahavalueserviceclient")
_HTTP_INTERFACE_BINARY = _RELEASE_DIR / ("yahahttpmqttinterfaceclient.exe" if os.name == "nt" else "yahahttpmqttinterfaceclient")


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _run_or_raise(command: list[str], label: str) -> None:
    completed = subprocess.run(
        command,
        cwd=_PROJECT_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode == 0:
        return

    output_text = "\n".join(
        section.strip() for section in [completed.stdout, completed.stderr] if section and section.strip()
    )
    raise RuntimeError(f"{label} failed with exit code {completed.returncode}: {output_text}")


def _wait_for_listener(host: str, port: int, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)

    raise TimeoutError(f"listener did not become reachable on {host}:{port}")


def _wait_for_filestore_ready(*, host: str, port: int, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            request = urllib.request.Request(f"http://{host}:{port}/healthz", method="GET")
            with urllib.request.urlopen(request, timeout=0.6):
                pass
            return
        except urllib.error.HTTPError as error:
            if int(error.code) in (400, 404):
                return
        except (urllib.error.URLError, TimeoutError):
            time.sleep(0.1)

    raise TimeoutError(f"filestore endpoint did not become ready on {host}:{port}")


def _write_filestore_ini(
    *,
    ini_path: Path,
    broker_host: str,
    broker_port: int,
    server_port: int,
    data_dir: Path,
    monitor_prefix: str,
) -> None:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str

    parser["mqtt"] = {
        "host": broker_host,
        "port": str(broker_port),
        "clientId": f"integration-filestore-{uuid.uuid4().hex[:10]}",
        "reconnectDelayMs": "50",
        "keepAliveIntervalMs": "1000",
        "loopSleepMs": "10",
        "enableLifecycleTrace": "false",
        "enableMessageTrace": "false",
    }
    parser["server"] = {
        "host": "127.0.0.1",
        "port": str(server_port),
    }
    parser["filestore"] = {
        "directory": str(data_dir),
        "keepFiles": "2",
        "maxKeyLength": "255",
    }
    parser["monitoring"] = {
        "enabled": "true",
        "topicPrefix": monitor_prefix,
        "qos": "1",
        "retain": "false",
        "watchIntervalMs": "50",
    }

    with ini_path.open("w", encoding="utf-8") as ini_file:
        parser.write(ini_file)


def _write_value_service_ini(
    *,
    ini_path: Path,
    broker_host: str,
    broker_port: int,
    filestore_port: int,
    values_key_path: str,
    monitor_prefix: str,
) -> None:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str

    parser["mqtt"] = {
        "host": broker_host,
        "port": str(broker_port),
        "clientId": f"integration-valueservice-{uuid.uuid4().hex[:10]}",
        "reconnectDelayMs": "50",
        "keepAliveIntervalMs": "1000",
        "loopSleepMs": "10",
    }
    parser["filestore"] = {
        "use": "true",
        "host": "127.0.0.1",
        "port": str(filestore_port),
        "filename": values_key_path,
        "topicPrefix": monitor_prefix,
    }
    parser["valueservice"] = {
        "subscribeQoS": "1",
    }

    with ini_path.open("w", encoding="utf-8") as ini_file:
        parser.write(ini_file)


def _write_http_interface_ini(*, ini_path: Path, http_port: int, broker_host: str, broker_port: int) -> None:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str

    parser["httpMqttInterface"] = {
        "listenerHost": "127.0.0.1",
        "listenerPort": str(http_port),
        "enablePublishPhpAlias": "true",
        "useLegacyPhpResponse": "false",
    }
    parser["mqtt"] = {
        "host": broker_host,
        "port": str(broker_port),
        "clientId": f"integration-http-interface-{uuid.uuid4().hex[:10]}",
        "reconnectDelayMs": "50",
        "keepAliveIntervalMs": "30000",
        "loopSleepMs": "10",
        "logReason": "false",
    }

    with ini_path.open("w", encoding="utf-8") as ini_file:
        parser.write(ini_file)


def _filestore_post_json(*, host: str, port: int, key_path: str, payload: dict[str, object]) -> None:
    request = urllib.request.Request(
        url=f"http://{host}:{port}{key_path}",
        data=json.dumps(payload, separators=(",", ":")).encode("utf-8"),
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=2.0) as response:
        status_code = int(response.getcode())
    if status_code != 200:
        raise RuntimeError(f"unexpected HTTP status from FileStore POST: {status_code}")


def _post_http_publish(*, http_port: int, topic: str, value_text: str) -> None:
    request_payload = {
        "topic": topic,
        "value": value_text,
        "reason": [{"message": "integration test", "timestamp": "2026-01-01T00:00:00Z"}],
    }
    request = urllib.request.Request(
        url=f"http://127.0.0.1:{http_port}/publish",
        data=json.dumps(request_payload, separators=(",", ":")).encode("utf-8"),
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=2.0) as response:
        status_code = int(response.getcode())
    if status_code != 204:
        raise RuntimeError(f"unexpected HTTP status from /publish: {status_code}")


def _collect_stream_lines(stream: TextIO | None) -> list[str]:
    if stream is None:
        return []

    lines: list[str] = []
    while True:
        ready_streams, _, _ = select.select([stream], [], [], 0.0)
        if not ready_streams:
            break
        line = stream.readline()
        if line == "":
            break
        lines.append(line.rstrip("\n"))
    return lines


def _wait_for_valueservice_in_log(
    process: subprocess.Popen[str],
    expected_topic: str,
    timeout_seconds: float,
) -> tuple[bool, list[str]]:
    deadline = time.monotonic() + timeout_seconds
    captured_lines: list[str] = []
    expected_fragment = f"value_service[in] topic={expected_topic}"

    while time.monotonic() < deadline:
        if process.poll() is not None:
            break

        stream = process.stdout
        if stream is None:
            break

        ready_streams, _, _ = select.select([stream], [], [], 0.05)
        if not ready_streams:
            continue

        line = stream.readline()
        if not line:
            continue

        line = line.rstrip("\n")
        captured_lines.append(line)
        if expected_fragment in line:
            return True, captured_lines

    captured_lines.extend(_collect_stream_lines(process.stdout))
    return False, captured_lines


def _terminate_process(process: subprocess.Popen[str] | None) -> None:
    if process is None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def run_http_publish_reaches_valuestore(config) -> tuple[bool, str]:
    broker_process = None
    filestore_process = None
    valueservice_process = None
    http_interface_process = None

    working_dir: Path | None = None

    # Reproduce the real field scenario topic path exactly.
    base_topic = "ground/wardrobe/floorHeating/lowtemperature"
    set_topic = f"{base_topic}/set"
    expected_value = "25"

    try:
        broker_port = _find_free_port()
        filestore_port = _find_free_port()
        http_port = _find_free_port()

        working_dir = Path(tempfile.mkdtemp(prefix="mqtt-http-valuestore-real-it-"))
        data_dir = working_dir / "data"
        data_dir.mkdir(parents=True, exist_ok=True)

        values_key_path = f"/valueservice/values/{uuid.uuid4().hex}"
        monitor_prefix = f"$MONITOR/FileStore/{uuid.uuid4().hex}"

        broker_overrides = {
            "network.mqtt_port": broker_port,
            "network.ws_port": 0,
            "broker.allow_anonymous": True,
        }
        broker_process = start_broker(broker_overrides)
        broker_host = resolve_target_host("127.0.0.1")

        _run_or_raise(
            [
                "cmake",
                "--build",
                "--preset",
                "release",
                "--target",
                "yahafilestoreclient",
                "yahavalueserviceclient",
                "yahahttpmqttinterfaceclient",
            ],
            "build integration clients",
        )
        if not _FILESTORE_BINARY.exists():
            return False, f"filestore binary missing: {_FILESTORE_BINARY}"
        if not _VALUE_SERVICE_BINARY.exists():
            return False, f"value service binary missing: {_VALUE_SERVICE_BINARY}"
        if not _HTTP_INTERFACE_BINARY.exists():
            return False, f"http interface binary missing: {_HTTP_INTERFACE_BINARY}"

        filestore_ini = working_dir / "filestore.ini"
        valueservice_ini = working_dir / "valueservice.ini"
        http_ini = working_dir / "http_interface.ini"

        _write_filestore_ini(
            ini_path=filestore_ini,
            broker_host=broker_host,
            broker_port=broker_port,
            server_port=filestore_port,
            data_dir=data_dir,
            monitor_prefix=monitor_prefix,
        )
        _write_value_service_ini(
            ini_path=valueservice_ini,
            broker_host=broker_host,
            broker_port=broker_port,
            filestore_port=filestore_port,
            values_key_path=values_key_path,
            monitor_prefix=monitor_prefix,
        )
        _write_http_interface_ini(
            ini_path=http_ini,
            http_port=http_port,
            broker_host=broker_host,
            broker_port=broker_port,
        )

        filestore_process = subprocess.Popen(
            [str(_FILESTORE_BINARY), str(filestore_ini)],
            cwd=working_dir,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        _wait_for_filestore_ready(
            host="127.0.0.1",
            port=filestore_port,
            timeout_seconds=max(2.0, config.timeout_seconds),
        )

        _filestore_post_json(
            host="127.0.0.1",
            port=filestore_port,
            key_path=values_key_path,
            payload={base_topic: "0"},
        )

        valueservice_process = subprocess.Popen(
            [str(_VALUE_SERVICE_BINARY), str(valueservice_ini)],
            cwd=working_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        http_interface_process = subprocess.Popen(
            [str(_HTTP_INTERFACE_BINARY), str(http_ini)],
            cwd=working_dir,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        _wait_for_listener("127.0.0.1", http_port, timeout_seconds=max(2.0, config.timeout_seconds))

        if valueservice_process.poll() is not None:
            return False, "yahavalueserviceclient exited before test publish"
        if http_interface_process.poll() is not None:
            return False, "yahahttpmqttinterfaceclient exited before test publish"

        with MqttClient(timeout_seconds=max(2.0, config.timeout_seconds)) as subscriber:
            connack = subscriber.connect(
                broker_host,
                broker_port,
                client_id=f"interop-sub-{uuid.uuid4().hex[:10]}",
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            subscriber.subscribe(f"{base_topic}/#", qos=1)
            subscriber.drain_available_messages()

            # Give ValueService a short window to complete initial subscriptions.
            time.sleep(0.4)

            _post_http_publish(http_port=http_port, topic=set_topic, value_text=expected_value)

            saw_in_log, collected_logs = _wait_for_valueservice_in_log(
                valueservice_process,
                expected_topic=set_topic,
                timeout_seconds=max(2.0, config.timeout_seconds),
            )
            if not saw_in_log:
                last_lines = "\n".join(collected_logs[-12:])
                return False, (
                    "ValueService did not log inbound /set message"
                    + (f"; recent logs:\n{last_lines}" if last_lines else "; no logs captured")
                )

            message = subscriber.collect_message_for_topic(
                expected_topic=base_topic,
                timeout=max(2.0, config.timeout_seconds),
            )
            actual_payload = message.payload.decode("utf-8", errors="replace")
            if actual_payload != expected_value:
                return False, (
                    "ValueService inbound /set was observed, but forwarded value was mutated"
                    f" (expected={expected_value!r}, actual={actual_payload!r})"
                )

        return True, "HTTP publish reached broker and ValueService logged inbound set topic"
    except Exception as error:
        return False, f"valuestore forwarding failed: {error}"
    finally:
        _terminate_process(http_interface_process)
        _terminate_process(valueservice_process)
        _terminate_process(filestore_process)
        stop_broker(broker_process)
        if working_dir is not None:
            shutil.rmtree(working_dir, ignore_errors=True)


TEST_CASES = [
    {
        "name": "interop/http_mqtt_interface/publish_reaches_valuestore",
        "description": "HTTP /publish reaches ValueService and updates value topic for real-world key",
        "run": run_http_publish_reaches_valuestore,
    },
]
