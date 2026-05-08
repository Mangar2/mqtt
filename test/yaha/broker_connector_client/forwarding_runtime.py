from __future__ import annotations

import json
import socket
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from http.client import HTTPConnection
from pathlib import Path
from typing import Callable

import sys

HELPERS_ROOT = Path(__file__).resolve().parents[2] / "integration_tests"
if str(HELPERS_ROOT) not in sys.path:
    sys.path.insert(0, str(HELPERS_ROOT))

from helpers.assertions import assert_connack, assert_message
from helpers.mqtt_client import MqttClient


class ProcessHandle:
    def __init__(self, process: subprocess.Popen, name: str) -> None:
        self.process = process
        self.name = name


class FakeSourceHttpBroker:
    def __init__(self, host: str, port: int) -> None:
        self._host = host
        self._port = port
        self._httpd: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None
        self._lock = threading.Lock()
        self._connect_calls = 0
        self._subscribe_calls = 0
        self._session_generation = 0
        self._send_token = ""
        self._receive_token = ""

    def start(self) -> None:
        if self._httpd is not None:
            return

        owner = self

        class Handler(BaseHTTPRequestHandler):
            server_version = "FakeSource/1.0"

            def do_PUT(self) -> None:  # noqa: N802
                body = self.rfile.read(int(self.headers.get("Content-Length", "0") or "0"))
                _ = body

                if self.path == "/connect":
                    with owner._lock:
                        owner._connect_calls += 1
                        owner._session_generation += 1
                        owner._send_token = f"send-token-{owner._session_generation}"
                        owner._receive_token = f"recv-token-{owner._session_generation}"
                    payload = {
                        "present": 0,
                        "token": {"send": owner._send_token, "receive": owner._receive_token},
                    }
                    self.send_response(200)
                    self.send_header("content-type", "application/json; charset=UTF-8")
                    self.send_header("packet", "connack")
                    self.send_header("version", "1.0")
                    self.end_headers()
                    self.wfile.write(json.dumps(payload).encode("utf-8"))
                    return

                if self.path == "/subscribe":
                    with owner._lock:
                        owner._subscribe_calls += 1
                    packet_id = self.headers.get("packetid", "1")
                    self.send_response(200)
                    self.send_header("content-type", "application/json; charset=UTF-8")
                    self.send_header("packet", "suback")
                    self.send_header("packetid", packet_id)
                    self.end_headers()
                    self.wfile.write(b'{"qos":[1]}')
                    return

                if self.path == "/pingreq":
                    token_ok = False
                    try:
                        data = json.loads(body.decode("utf-8")) if body else {}
                        token = data.get("token")
                        with owner._lock:
                            token_ok = isinstance(token, str) and token == owner._send_token and owner._send_token != ""
                    except Exception:
                        token_ok = False

                    if not token_ok:
                        self.send_response(401)
                        self.send_header("content-type", "application/json; charset=UTF-8")
                        self.end_headers()
                        self.wfile.write(b'{"error":"invalid_token"}')
                        return

                    self.send_response(204)
                    self.send_header("content-type", "application/json; charset=UTF-8")
                    self.send_header("packet", "pingresp")
                    self.end_headers()
                    return

                if self.path == "/disconnect":
                    self.send_response(204)
                    self.send_header("content-type", "application/json; charset=UTF-8")
                    self.end_headers()
                    return

                self.send_response(404)
                self.end_headers()

            def do_GET(self) -> None:  # noqa: N802
                if self.path == "/health":
                    self.send_response(200)
                    self.send_header("content-type", "text/plain")
                    self.end_headers()
                    self.wfile.write(b"ok")
                    return
                self.send_response(404)
                self.end_headers()

            def log_message(self, format: str, *args: object) -> None:
                return

        self._httpd = ThreadingHTTPServer((self._host, self._port), Handler)
        self._thread = threading.Thread(target=self._httpd.serve_forever, daemon=True)
        self._thread.start()
        self._wait_http_ready()

    def stop(self) -> None:
        if self._httpd is None:
            return
        self._httpd.shutdown()
        self._httpd.server_close()
        self._httpd = None
        if self._thread is not None:
            self._thread.join(timeout=3.0)
        self._thread = None
        with self._lock:
            self._send_token = ""
            self._receive_token = ""

    def restart(self) -> None:
        self.stop()
        self.start()

    def connect_calls(self) -> int:
        with self._lock:
            return self._connect_calls

    def subscribe_calls(self) -> int:
        with self._lock:
            return self._subscribe_calls

    def send_publish(self,
                     listener_port: int,
                     topic: str,
                     value: str,
                     qos: int = 1,
                     packet_id: int = 7) -> tuple[int, dict[str, str], str]:
        with self._lock:
            token = self._receive_token
        payload = {
            "token": token,
            "message": {
                "topic": topic,
                "value": value,
            },
        }
        return self.send_publish_body(
            listener_port=listener_port,
            body=json.dumps(payload),
            qos=qos,
            packet_id=packet_id,
        )

    def send_publish_body(self,
                          listener_port: int,
                          body: str,
                          qos: int = 1,
                          packet_id: int = 7) -> tuple[int, dict[str, str], str]:
        connection = HTTPConnection("127.0.0.1", listener_port, timeout=3.0)
        try:
            connection.request(
                "PUT",
                "/publish",
                body=body,
                headers={
                    "content-type": "application/json; charset=UTF-8",
                    "version": "1.0",
                    "qos": str(qos),
                    "retain": "0",
                    "dup": "0",
                    "packetid": str(packet_id),
                },
            )
            response = connection.getresponse()
            response_body = response.read().decode("utf-8", errors="replace")
            headers = {key.lower(): value for key, value in response.getheaders()}
            return response.status, headers, response_body
        finally:
            connection.close()

    def receive_token(self) -> str:
        with self._lock:
            return self._receive_token

    def _wait_http_ready(self, timeout: float = 4.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                connection = HTTPConnection(self._host, self._port, timeout=0.5)
                connection.request("GET", "/health")
                response = connection.getresponse()
                response.read()
                connection.close()
                if response.status == 200:
                    return
            except Exception:
                pass
            time.sleep(0.05)
        raise TimeoutError("fake source HTTP broker did not become ready")


def _reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as temp_socket:
        temp_socket.bind(("127.0.0.1", 0))
        return int(temp_socket.getsockname()[1])


def _wait_until(predicate: Callable[[], bool], timeout_seconds: float, step_seconds: float = 0.05) -> bool:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(step_seconds)
    return predicate()


def _wait_tcp_ready(host: str, port: int, timeout_seconds: float = 8.0) -> None:
    if not _wait_until(lambda: _is_tcp_ready(host, port), timeout_seconds):
        raise TimeoutError(f"TCP endpoint not ready: {host}:{port}")


def _is_tcp_ready(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.5):
            return True
    except OSError:
        return False


def _start_process(command: list[str], name: str, cwd: Path) -> ProcessHandle:
    process = subprocess.Popen(
        command,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return ProcessHandle(process=process, name=name)


def _stop_process(handle: ProcessHandle) -> None:
    process = handle.process
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5.0)


def _build_paths() -> tuple[Path, Path, Path]:
    root = Path(__file__).resolve().parents[3]
    broker_exe = root / "build" / "debug" / "yahabroker"
    connector_exe = root / "build" / "debug" / "yahabrokerconnectorclient"
    return root, broker_exe, connector_exe


def _write_broker_config(path: Path, mqtt_port: int) -> None:
    path.write_text(
        "\n".join(
            [
                "[network]",
                f"mqtt_port = {mqtt_port}",
                "ws_port = 0",
                "",
                "[broker]",
                "allow_anonymous = true",
                "max_connections = 200",
                "receive_maximum = 65535",
                "server_keep_alive = 0",
                "session_expiry_max = 0",
                "topic_alias_maximum = 10",
                "max_queued_messages = 1000",
                "write_queue_max_bytes = 65536",
                "stream_buffer_max_bytes = 1048576",
                "qos_retransmit_timeout_seconds = 5",
                "tick_interval_ms = 50",
                "",
                "[persistence]",
                "mode = off",
                "dir = ./data",
                "",
                "[tracing]",
                "global_level = warning",
                "trace_modules =",
                "max_text_length = 2048",
                "",
            ]
        ),
        encoding="utf-8",
    )


def _write_connector_config(path: Path,
                            source_port: int,
                            source_listener_port: int,
                            target_mqtt_port: int,
                            max_publish_retries: int = 5,
                            publish_retry_backoff_ms: int = 100) -> None:
    path.write_text(
        "\n".join(
            [
                "[sourceHttpBroker]",
                "host = 127.0.0.1",
                f"port = {source_port}",
                "clientId = broker-connector-source-it",
                "clean = true",
                "keepAliveSeconds = 3",
                "listenerHost = 127.0.0.1",
                f"listenerPort = {source_listener_port}",
                "",
                "[subscription]",
                "topic = bridge/#",
                "qos = 1",
                "",
                "[receiverMqttBroker]",
                "host = 127.0.0.1",
                f"port = {target_mqtt_port}",
                "clientId = broker-connector-receiver-it",
                "reconnectDelayMs = 200",
                "keepAliveSeconds = 5",
                "loopSleepMs = 20",
                "enableLifecycleTrace = false",
                "enableMessageTrace = false",
                "",
                "[automation]",
                "reconnectDelayMs = 200",
                "sourceLoopSleepMs = 20",
                "sourceKeepAliveIntervalMs = 1000",
                f"maxPublishRetries = {max_publish_retries}",
                f"publishRetryBackoffMs = {publish_retry_backoff_ms}",
                "normalizeQosToAtLeastOnce = true",
                "retainPassthrough = true",
                "",
                "[monitoring]",
                "sourceLifecycleTrace = false",
                "",
            ]
        ),
        encoding="utf-8",
    )


def _start_target_broker(root: Path, broker_exe: Path, config_path: Path, mqtt_port: int) -> ProcessHandle:
    handle = _start_process([str(broker_exe), str(config_path)], "target-broker", root)
    _wait_tcp_ready("127.0.0.1", mqtt_port)
    return handle


def _start_connector(root: Path, connector_exe: Path, config_path: Path) -> ProcessHandle:
    return _start_process([str(connector_exe), str(config_path)], "broker-connector", root)


def _assert_process_alive(handle: ProcessHandle, context: str) -> None:
    if handle.process.poll() is not None:
        stdout_text = handle.process.stdout.read() if handle.process.stdout else ""
        stderr_text = handle.process.stderr.read() if handle.process.stderr else ""
        raise AssertionError(
            f"{handle.name} exited unexpectedly during {context}.\n"
            f"stdout:\n{stdout_text}\n"
            f"stderr:\n{stderr_text}\n"
        )


def _run_forwarding_case() -> tuple[bool, str]:
    root, broker_exe, connector_exe = _build_paths()
    if not broker_exe.exists() or not connector_exe.exists():
        return False, "required debug binaries missing: build/debug/yahabroker or yahabrokerconnectorclient"

    target_port = _reserve_port()
    source_port = _reserve_port()
    listener_port = _reserve_port()

    with tempfile.TemporaryDirectory(prefix="yaha_connector_it_") as temp_dir_str:
        temp_dir = Path(temp_dir_str)
        broker_cfg = temp_dir / "broker.ini"
        connector_cfg = temp_dir / "connector.ini"
        _write_broker_config(broker_cfg, target_port)
        _write_connector_config(connector_cfg, source_port, listener_port, target_port)

        source = FakeSourceHttpBroker("127.0.0.1", source_port)
        source.start()
        broker_handle = _start_target_broker(root, broker_exe, broker_cfg, target_port)
        connector_handle = _start_connector(root, connector_exe, connector_cfg)

        try:
            if not _wait_until(lambda: source.connect_calls() >= 1 and source.subscribe_calls() >= 1, 6.0):
                _assert_process_alive(connector_handle, "waiting for source connect+subscribe")
                return False, "connector did not complete source connect+subscribe"

            with MqttClient(client_id="it-sub-forward") as subscriber:
                connack = subscriber.connect("127.0.0.1", target_port)
                assert_connack(connack, reason_code=0x00, session_present=False)
                subscriber.subscribe("bridge/test", qos=1)

                expected_payload = json.dumps(
                    {
                        "token": source.receive_token(),
                        "message": {
                            "topic": "bridge/test",
                            "value": "hello-bridge",
                        },
                    }
                ).encode("utf-8")

                status, headers, _ = source.send_publish(
                    listener_port=listener_port,
                    topic="bridge/test",
                    value="hello-bridge",
                    qos=1,
                    packet_id=11,
                )
                if status != 204 or headers.get("packet") != "puback":
                    return False, f"source callback ack mismatch: status={status}, headers={headers}"

                messages = subscriber.collect_messages(count=1, timeout=6.0)
                assert_message(messages[0], topic="bridge/test", payload=expected_payload, qos=1, retain=False)

            _assert_process_alive(connector_handle, "forwarding case completion")
            return True, "source publish forwarded to target broker"
        except Exception as exception_value:
            return False, str(exception_value)
        finally:
            _stop_process(connector_handle)
            _stop_process(broker_handle)
            source.stop()


def _run_source_restart_case() -> tuple[bool, str]:
    root, broker_exe, connector_exe = _build_paths()
    if not broker_exe.exists() or not connector_exe.exists():
        return False, "required debug binaries missing: build/debug/yahabroker or yahabrokerconnectorclient"

    target_port = _reserve_port()
    source_port = _reserve_port()
    listener_port = _reserve_port()

    with tempfile.TemporaryDirectory(prefix="yaha_connector_it_restart_") as temp_dir_str:
        temp_dir = Path(temp_dir_str)
        broker_cfg = temp_dir / "broker.ini"
        connector_cfg = temp_dir / "connector.ini"
        _write_broker_config(broker_cfg, target_port)
        _write_connector_config(connector_cfg, source_port, listener_port, target_port)

        source = FakeSourceHttpBroker("127.0.0.1", source_port)
        source.start()
        broker_handle = _start_target_broker(root, broker_exe, broker_cfg, target_port)
        connector_handle = _start_connector(root, connector_exe, connector_cfg)

        try:
            if not _wait_until(lambda: source.connect_calls() >= 1 and source.subscribe_calls() >= 1, 6.0):
                return False, "initial source connect+subscribe not observed"

            source.restart()

            if not _wait_until(lambda: source.connect_calls() >= 2 and source.subscribe_calls() >= 2, 20.0):
                _assert_process_alive(connector_handle, "waiting for source reconnect+resubscribe")
                return False, (
                    "source reconnect/resubscribe not observed after source restart "
                    f"(connect_calls={source.connect_calls()}, subscribe_calls={source.subscribe_calls()})"
                )

            with MqttClient(client_id="it-sub-restart") as subscriber:
                connack = subscriber.connect("127.0.0.1", target_port)
                assert_connack(connack, reason_code=0x00, session_present=False)
                subscriber.subscribe("bridge/restart", qos=1)

                expected_payload = json.dumps(
                    {
                        "token": source.receive_token(),
                        "message": {
                            "topic": "bridge/restart",
                            "value": "after-restart",
                        },
                    }
                ).encode("utf-8")

                status, headers, _ = source.send_publish(
                    listener_port=listener_port,
                    topic="bridge/restart",
                    value="after-restart",
                    qos=1,
                    packet_id=22,
                )
                if status != 204 or headers.get("packet") != "puback":
                    return False, f"source callback ack mismatch after restart: status={status}, headers={headers}"

                messages = subscriber.collect_messages(count=1, timeout=6.0)
                assert_message(messages[0], topic="bridge/restart", payload=expected_payload, qos=1, retain=False)

            _assert_process_alive(connector_handle, "source restart case completion")
            return True, "source restart triggers reconnect and resubscribe with forwarding still active"
        except Exception as exception_value:
            return False, str(exception_value)
        finally:
            _stop_process(connector_handle)
            _stop_process(broker_handle)
            source.stop()


def _run_target_outage_recovery_case() -> tuple[bool, str]:
    root, broker_exe, connector_exe = _build_paths()
    if not broker_exe.exists() or not connector_exe.exists():
        return False, "required debug binaries missing: build/debug/yahabroker or yahabrokerconnectorclient"

    target_port = _reserve_port()
    source_port = _reserve_port()
    listener_port = _reserve_port()

    with tempfile.TemporaryDirectory(prefix="yaha_connector_it_target_outage_") as temp_dir_str:
        temp_dir = Path(temp_dir_str)
        broker_cfg = temp_dir / "broker.ini"
        connector_cfg = temp_dir / "connector.ini"
        _write_broker_config(broker_cfg, target_port)
        _write_connector_config(
            connector_cfg,
            source_port,
            listener_port,
            target_port,
            max_publish_retries=20,
            publish_retry_backoff_ms=100,
        )

        source = FakeSourceHttpBroker("127.0.0.1", source_port)
        source.start()
        broker_handle = _start_target_broker(root, broker_exe, broker_cfg, target_port)
        connector_handle = _start_connector(root, connector_exe, connector_cfg)

        try:
            if not _wait_until(lambda: source.connect_calls() >= 1 and source.subscribe_calls() >= 1, 6.0):
                return False, "initial source connect+subscribe not observed"

            _stop_process(broker_handle)
            time.sleep(0.4)

            _assert_process_alive(connector_handle, "target outage window")

            broker_handle = _start_target_broker(root, broker_exe, broker_cfg, target_port)

            with MqttClient(client_id="it-sub-outage") as subscriber:
                connack = subscriber.connect("127.0.0.1", target_port)
                assert_connack(connack, reason_code=0x00, session_present=False)
                subscriber.subscribe("bridge/outage", qos=1)

                expected_payload = json.dumps(
                    {
                        "token": source.receive_token(),
                        "message": {
                            "topic": "bridge/outage",
                            "value": "after-target-recovery",
                        },
                    }
                ).encode("utf-8")

                status, headers, _ = source.send_publish(
                    listener_port=listener_port,
                    topic="bridge/outage",
                    value="after-target-recovery",
                    qos=1,
                    packet_id=33,
                )
                if status != 204 or headers.get("packet") != "puback":
                    return False, f"source callback ack mismatch after target recovery: status={status}, headers={headers}"

                messages = subscriber.collect_messages(count=1, timeout=8.0)
                assert_message(messages[0], topic="bridge/outage", payload=expected_payload, qos=1, retain=False)

            _assert_process_alive(connector_handle, "target outage recovery case completion")
            return True, "connector survives target outage and forwards again after broker recovery"
        except Exception as exception_value:
            return False, str(exception_value)
        finally:
            _stop_process(connector_handle)
            _stop_process(broker_handle)
            source.stop()


def _run_payload_passthrough_case() -> tuple[bool, str]:
    root, broker_exe, connector_exe = _build_paths()
    if not broker_exe.exists() or not connector_exe.exists():
        return False, "required debug binaries missing: build/debug/yahabroker or yahabrokerconnectorclient"

    target_port = _reserve_port()
    source_port = _reserve_port()
    listener_port = _reserve_port()

    with tempfile.TemporaryDirectory(prefix="yaha_connector_it_payload_passthrough_") as temp_dir_str:
        temp_dir = Path(temp_dir_str)
        broker_cfg = temp_dir / "broker.ini"
        connector_cfg = temp_dir / "connector.ini"
        _write_broker_config(broker_cfg, target_port)
        _write_connector_config(connector_cfg, source_port, listener_port, target_port)

        source = FakeSourceHttpBroker("127.0.0.1", source_port)
        source.start()
        broker_handle = _start_target_broker(root, broker_exe, broker_cfg, target_port)
        connector_handle = _start_connector(root, connector_exe, connector_cfg)

        try:
            if not _wait_until(lambda: source.connect_calls() >= 1 and source.subscribe_calls() >= 1, 6.0):
                _assert_process_alive(connector_handle, "waiting for source connect+subscribe")
                return False, "connector did not complete source connect+subscribe"

            receive_token = source.receive_token()
            if not receive_token:
                return False, "source receive token not available"

            raw_body = (
                "{\n"
                f"  \"token\":\"{receive_token}\",\n"
                "  \"message\":{\n"
                "    \"topic\":\"bridge/raw\",\n"
                "    \"value\":\"sensor\",\n"
                "    \"reason\":[{\"message\":\"from-msgstore\",\"timestamp\":\"2026-05-08T10:00:00Z\"}]\n"
                "  }\n"
                "}"
            )

            with MqttClient(client_id="it-sub-raw-payload") as subscriber:
                connack = subscriber.connect("127.0.0.1", target_port)
                assert_connack(connack, reason_code=0x00, session_present=False)
                subscriber.subscribe("bridge/raw", qos=1)

                status, headers, _ = source.send_publish_body(
                    listener_port=listener_port,
                    body=raw_body,
                    qos=1,
                    packet_id=44,
                )
                if status != 204 or headers.get("packet") != "puback":
                    return False, f"source callback ack mismatch for raw payload: status={status}, headers={headers}"

                messages = subscriber.collect_messages(count=1, timeout=6.0)
                assert_message(
                    messages[0],
                    topic="bridge/raw",
                    payload=raw_body.encode("utf-8"),
                    qos=1,
                    retain=False,
                )

            _assert_process_alive(connector_handle, "raw payload passthrough case completion")
            return True, "connector forwards source envelope payload bytes unchanged"
        except Exception as exception_value:
            return False, str(exception_value)
        finally:
            _stop_process(connector_handle)
            _stop_process(broker_handle)
            source.stop()


TEST_CASES = [
    {
        "name": "broker_connector/forwarding_source_to_target",
        "description": "Forwards source callback publish to target MQTT broker.",
        "run": lambda _config: _run_forwarding_case(),
    },
    {
        "name": "broker_connector/source_restart_reconnect_resubscribe",
        "description": "Re-connects and re-subscribes after source HTTP broker restart.",
        "run": lambda _config: _run_source_restart_case(),
    },
    {
        "name": "broker_connector/target_outage_recovery",
        "description": "Continues operating across temporary target broker outage and recovery.",
        "run": lambda _config: _run_target_outage_recovery_case(),
    },
    {
        "name": "broker_connector/source_payload_passthrough_exact_bytes",
        "description": "Forwards source HTTP envelope payload to MQTT unchanged when token and topic are valid.",
        "run": lambda _config: _run_payload_passthrough_case(),
    },
]
