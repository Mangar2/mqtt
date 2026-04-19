"""Integration tests for graceful shutdown section 16.1."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import shutil
import signal
import socket
import tempfile
import time
import uuid


def _load_helper(module_name: str):
    helper_path = Path(__file__).resolve().parents[1] / "helpers" / f"{module_name}.py"
    spec = importlib.util.spec_from_file_location(f"integration_helper_{module_name}", helper_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load helper module {module_name} from {helper_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_assertions_module = _load_helper("assertions")
_broker_module = _load_helper("broker")
_mqtt_client_module = _load_helper("mqtt_client")

assert_connack = _assertions_module.assert_connack
assert_disconnected = _assertions_module.assert_disconnected
assert_message = _assertions_module.assert_message
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
PacketTypes = _mqtt_client_module.PacketTypes
Properties = _mqtt_client_module.Properties


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _unique_topic(prefix: str) -> str:
    return f"integration/shutdown/{prefix}/{uuid.uuid4().hex}"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _new_connect_properties(**values):
    properties = Properties(PacketTypes.CONNECT)
    for key_name, value in values.items():
        setattr(properties, key_name, value)
    return properties


def _start_isolated_broker(overrides: dict[str, object] | None = None):
    effective_overrides: dict[str, object] = {
        "network.mqtt_port": _find_free_port(),
        "network.ws_port": 0,
        "broker.allow_anonymous": True,
    }
    if overrides is not None:
        effective_overrides.update(overrides)

    process = start_broker(effective_overrides)
    return "127.0.0.1", int(effective_overrides["network.mqtt_port"]), process, effective_overrides


def _wait_for_process_exit(process, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            return
        time.sleep(0.05)
    raise TimeoutError(f"broker process did not exit within {timeout_seconds:.1f}s")


def _send_shutdown_signal(process, shutdown_signal: int) -> None:
    if process.poll() is not None:
        raise RuntimeError("broker process is already stopped")
    process.send_signal(shutdown_signal)


def _connect_client(host: str, port: int, timeout_seconds: float, client_id: str) -> MqttClient:
    client = MqttClient(timeout_seconds=timeout_seconds)
    connack = client.connect(host, port, client_id=client_id, clean_start=True)
    assert_connack(connack, reason_code=0x00, session_present=False)
    return client


def run_16_1_1_sigterm_sends_disconnect_8b_to_all_clients(config) -> tuple[bool, str]:
    process = None
    clients: list[MqttClient] = []

    try:
        host, port, process, _ = _start_isolated_broker()

        for index in range(3):
            client = _connect_client(
                host,
                port,
                config.timeout_seconds,
                client_id=_unique_client_id(f"shutdown-sigterm-{index}"),
            )
            clients.append(client)

        _send_shutdown_signal(process, signal.SIGTERM)

        for client in clients:
            assert_disconnected(client, reason_code=0x8B, timeout=max(5.0, config.timeout_seconds))

        _wait_for_process_exit(process, timeout_seconds=max(5.0, config.timeout_seconds))

        return True, "16.1.1 SIGTERM sent DISCONNECT 0x8B to all connected clients"
    except Exception as error:
        return False, f"16.1.1 failed: {error}"
    finally:
        for client in clients:
            try:
                client.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_16_1_2_connections_close_cleanly_before_exit(config) -> tuple[bool, str]:
    process = None
    clients: list[MqttClient] = []

    try:
        host, port, process, _ = _start_isolated_broker()

        for index in range(2):
            client = _connect_client(
                host,
                port,
                config.timeout_seconds,
                client_id=_unique_client_id(f"shutdown-clean-close-{index}"),
            )
            clients.append(client)

        _send_shutdown_signal(process, signal.SIGTERM)

        disconnect_deadline = max(5.0, config.timeout_seconds)
        for client in clients:
            client.wait_for_disconnect(disconnect_deadline)

        _wait_for_process_exit(process, timeout_seconds=max(5.0, config.timeout_seconds))

        return True, "16.1.2 broker closed all client connections cleanly before process exit"
    except Exception as error:
        return False, f"16.1.2 failed: {error}"
    finally:
        for client in clients:
            try:
                client.disconnect()
            except Exception:
                pass
        stop_broker(process)


def run_16_1_3_persistence_flushed_before_exit(config) -> tuple[bool, str]:
    process = None
    data_dir = Path(tempfile.mkdtemp(prefix="mqtt-integration-shutdown-"))
    durable_client_id = _unique_client_id("shutdown-durable")
    queued_topic = _unique_topic("queued")
    retained_topic = _unique_topic("retained")
    post_restart_payload = b"published-after-restart"
    retained_payload = b"retained-before-shutdown"

    try:
        host, port, process, startup_overrides = _start_isolated_broker(
            {
                "persistence.enabled": True,
                "persistence.dir": str(data_dir),
            }
        )

        with MqttClient(timeout_seconds=config.timeout_seconds) as durable_client:
            connack = durable_client.connect(
                host,
                port,
                client_id=durable_client_id,
                clean_start=True,
                properties=_new_connect_properties(SessionExpiryInterval=600),
            )
            assert_connack(connack, reason_code=0x00, session_present=False)

            suback_codes = durable_client.subscribe(queued_topic, qos=1)
            if not suback_codes:
                return False, "missing SUBACK while preparing durable session"
            assert_reason_code(suback_codes[0], 0x01)

            durable_client.disconnect(reason_code=0x00)

        with MqttClient(timeout_seconds=config.timeout_seconds) as publisher:
            pub_connack = publisher.connect(
                host,
                port,
                client_id=_unique_client_id("shutdown-publisher"),
                clean_start=True,
            )
            assert_connack(pub_connack, reason_code=0x00, session_present=False)

            retained_reason = publisher.publish(retained_topic, retained_payload, qos=1, retain=True)
            if int(retained_reason) not in (0x00, 0x10):
                return False, f"unexpected PUBACK reason for retained message: 0x{int(retained_reason):02X}"

        _send_shutdown_signal(process, signal.SIGTERM)
        _wait_for_process_exit(process, timeout_seconds=max(5.0, config.timeout_seconds))

        process = start_broker(startup_overrides)

        with MqttClient(timeout_seconds=config.timeout_seconds) as resumed_client:
            resumed_connack = resumed_client.connect(
                host,
                port,
                client_id=durable_client_id,
                clean_start=False,
            )
            assert_connack(resumed_connack, reason_code=0x00, session_present=True)

            with MqttClient(timeout_seconds=config.timeout_seconds) as restart_publisher:
                restart_pub_connack = restart_publisher.connect(
                    host,
                    port,
                    client_id=_unique_client_id("shutdown-post-restart-publisher"),
                    clean_start=True,
                )
                assert_connack(restart_pub_connack, reason_code=0x00, session_present=False)
                publish_reason = restart_publisher.publish(
                    queued_topic,
                    post_restart_payload,
                    qos=1,
                    retain=False,
                )
                if int(publish_reason) not in (0x00, 0x10):
                    return (
                        False,
                        "unexpected PUBACK reason for post-restart publish: "
                        f"0x{int(publish_reason):02X}",
                    )

            resumed_messages = resumed_client.collect_messages(
                count=1,
                timeout=max(3.0, config.timeout_seconds),
            )
            assert_message(
                resumed_messages[0],
                topic=queued_topic,
                payload=post_restart_payload,
                qos=1,
                retain=False,
            )

        with MqttClient(timeout_seconds=config.timeout_seconds) as retained_observer:
            observer_connack = retained_observer.connect(
                host,
                port,
                client_id=_unique_client_id("shutdown-retained-observer"),
                clean_start=True,
            )
            assert_connack(observer_connack, reason_code=0x00, session_present=False)

            retained_suback = retained_observer.subscribe(retained_topic, qos=1)
            if not retained_suback:
                return False, "missing SUBACK while validating retained persistence"
            assert_reason_code(retained_suback[0], 0x01)

            retained_messages = retained_observer.collect_messages(
                count=1,
                timeout=max(3.0, config.timeout_seconds),
            )
            retained_message = retained_messages[0]
            if retained_message.topic != retained_topic:
                return (
                    False,
                    "retained topic mismatch after restart: "
                    f"expected {retained_topic!r}, got {retained_message.topic!r}",
                )
            if retained_message.payload != retained_payload:
                return (
                    False,
                    "retained payload mismatch after restart: "
                    f"expected {retained_payload!r}, got {retained_message.payload!r}",
                )
            if int(retained_message.qos) != 1:
                return (
                    False,
                    "retained qos mismatch after restart: "
                    f"expected 1, got {int(retained_message.qos)}",
                )

        return True, "16.1.3 shutdown flushed persisted session queue and retained messages"
    except Exception as error:
        return False, f"16.1.3 failed: {error}"
    finally:
        stop_broker(process)
        shutil.rmtree(data_dir, ignore_errors=True)


def run_16_1_4_sigint_behaves_like_sigterm(config) -> tuple[bool, str]:
    process = None
    clients: list[MqttClient] = []

    try:
        host, port, process, _ = _start_isolated_broker()

        for index in range(2):
            client = _connect_client(
                host,
                port,
                config.timeout_seconds,
                client_id=_unique_client_id(f"shutdown-sigint-{index}"),
            )
            clients.append(client)

        _send_shutdown_signal(process, signal.SIGINT)

        for client in clients:
            assert_disconnected(client, reason_code=0x8B, timeout=max(5.0, config.timeout_seconds))

        _wait_for_process_exit(process, timeout_seconds=max(5.0, config.timeout_seconds))

        return True, "16.1.4 SIGINT matched SIGTERM behavior with DISCONNECT 0x8B"
    except Exception as error:
        return False, f"16.1.4 failed: {error}"
    finally:
        for client in clients:
            try:
                client.disconnect()
            except Exception:
                pass
        stop_broker(process)


TEST_CASES = [
    {
        "name": "shutdown/graceful/sigterm_sends_disconnect_8b_to_all_clients",
        "description": "16.1.1 SIGTERM sends DISCONNECT 0x8B to all clients",
        "run": run_16_1_1_sigterm_sends_disconnect_8b_to_all_clients,
    },
    {
        "name": "shutdown/graceful/connections_close_cleanly_before_exit",
        "description": "16.1.2 All connections close cleanly before process exits",
        "run": run_16_1_2_connections_close_cleanly_before_exit,
    },
    {
        "name": "shutdown/graceful/persistence_flushed_before_exit",
        "description": "16.1.3 Persistence flushes sessions and retained messages before exit",
        "run": run_16_1_3_persistence_flushed_before_exit,
    },
    {
        "name": "shutdown/graceful/sigint_behaves_like_sigterm",
        "description": "16.1.4 SIGINT behaves like SIGTERM",
        "run": run_16_1_4_sigint_behaves_like_sigterm,
    },
]
