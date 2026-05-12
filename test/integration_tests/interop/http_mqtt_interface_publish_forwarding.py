"""Integration test: HTTP MQTT interface publish is forwarded to broker subscribers."""

from __future__ import annotations

import configparser
import importlib.util
import socket
import subprocess
import tempfile
import time
import urllib.request
import uuid
from pathlib import Path


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
assert_reason_code = _assertions_module.assert_reason_code
assert_message = _assertions_module.assert_message

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
_RELEASE_DIR = _PROJECT_ROOT / "build" / "release"
_HTTP_INTERFACE_BINARY = _RELEASE_DIR / ("yahahttpmqttinterfaceclient.exe" if __import__("os").name == "nt" else "yahahttpmqttinterfaceclient")


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:10]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/http-forward/{prefix}/{uuid.uuid4().hex}"


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


def _write_http_interface_ini(http_port: int, broker_host: str, broker_port: int) -> Path:
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
        "clientId": "integration-http-interface-client",
        "reconnectDelayMs": "50",
        "keepAliveMs": "30000",
        "loopSleepMs": "10",
        "enableLifecycleTrace": "false",
        "enableMessageTrace": "false",
        "logReason": "false",
    }

    temp_dir = Path(tempfile.mkdtemp(prefix="mqtt-http-interface-it-"))
    ini_path = temp_dir / "broker.ini"
    with ini_path.open("w", encoding="utf-8") as ini_file:
        parser.write(ini_file)
    return ini_path


def _wait_for_listener(host: str, port: int, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)

    raise TimeoutError(f"HTTP listener did not become reachable on {host}:{port}")


def _post_publish(http_port: int, topic: str, value: str) -> None:
    payload_text = (
        "{"
        f'"topic":"{topic}",' 
        f'"value":"{value}",' 
        '"reason":[{"message":"integration test","timestamp":"2026-01-01T00:00:00Z"}]'
        "}"
    )
    request = urllib.request.Request(
        url=f"http://127.0.0.1:{http_port}/publish",
        data=payload_text.encode("utf-8"),
        method="POST",
        headers={"Content-Type": "application/json"},
    )

    with urllib.request.urlopen(request, timeout=2.0) as response:
        status_code = int(response.getcode())
    if status_code != 204:
        raise RuntimeError(f"unexpected HTTP status from /publish: {status_code}")


def run_http_mqtt_interface_publish_is_forwarded_to_broker(config) -> tuple[bool, str]:
    broker_process = None
    interface_process = None
    ini_path: Path | None = None
    topic = _unique_topic("forward")
    payload = b"23"

    broker_port = _find_free_port()
    http_port = _find_free_port()

    try:
        broker_overrides = {
            "network.mqtt_port": broker_port,
            "network.ws_port": 0,
            "broker.allow_anonymous": True,
        }
        broker_process = start_broker(broker_overrides)
        broker_host = resolve_target_host("127.0.0.1")

        _run_or_raise(
            ["cmake", "--build", "--preset", "release", "--target", "yahahttpmqttinterfaceclient"],
            "build yahahttpmqttinterfaceclient",
        )
        if not _HTTP_INTERFACE_BINARY.exists():
            return False, f"http interface binary missing: {_HTTP_INTERFACE_BINARY}"

        ini_path = _write_http_interface_ini(http_port=http_port, broker_host=broker_host, broker_port=broker_port)
        interface_process = subprocess.Popen(
            [str(_HTTP_INTERFACE_BINARY), str(ini_path)],
            cwd=_PROJECT_ROOT,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        _wait_for_listener("127.0.0.1", http_port, timeout_seconds=max(1.0, config.timeout_seconds))

        with MqttClient(timeout_seconds=config.timeout_seconds) as subscriber:
            connack = subscriber.connect(
                broker_host,
                broker_port,
                client_id=_unique_client_id("sub-http-forward"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)

            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "SUBACK is empty"
            assert_reason_code(int(suback_codes[0]), 0x01)

            _post_publish(http_port=http_port, topic=topic, value="23")
            messages = subscriber.collect_message_for_topic(expected_topic=topic, timeout=config.timeout_seconds)
            assert_message(messages, topic=topic, payload=payload, qos=1, retain=False)

        return True, "HTTP /publish was forwarded to broker and delivered to MQTT subscriber"
    except Exception as error:
        return False, f"http interface forwarding failed: {error}"
    finally:
        if interface_process is not None:
            interface_process.terminate()
            try:
                interface_process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                interface_process.kill()
                interface_process.wait(timeout=3)
        if ini_path is not None:
            try:
                import shutil
                shutil.rmtree(ini_path.parent, ignore_errors=True)
            except Exception:
                pass
        stop_broker(broker_process)


TEST_CASES = [
    {
        "name": "interop/http_mqtt_interface/publish_forwarded_to_broker",
        "description": "HTTP /publish forwards mapped message to broker and reaches MQTT subscriber",
        "run": run_http_mqtt_interface_publish_is_forwarded_to_broker,
    },
]
