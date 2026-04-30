"""Integration tests for robustness section 19.4 (Recovery)."""

from __future__ import annotations

import json
import importlib.util
import os
from pathlib import Path
import subprocess
import shutil
import socket
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
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
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
restart_broker = _broker_module.restart_broker
MqttClient = _mqtt_client_module.MqttClient
PacketTypes = _mqtt_client_module.PacketTypes
Properties = _mqtt_client_module.Properties
_SESSION_EXPIRY_SECONDS = 600
_BROKER_MANAGED_ENV = "MQTT_INTEGRATION_BROKER_MANAGED"
_PROJECT_ROOT = Path(__file__).resolve().parents[3]
_RELEASE_DIR = _PROJECT_ROOT / "build" / "release"
_MSGSTORE_BINARY = _RELEASE_DIR / ("yahamsgstoreclient.exe" if os.name == "nt" else "yahamsgstoreclient")


def _unique_client_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _require_managed_broker_in_remote(required_overrides: str) -> None:
    if os.environ.get(_BROKER_MANAGED_ENV, "").strip() != "0":
        return
    raise _broker_module.ManagedBrokerRequired(
        f"requires managed broker startup (requested overrides: {required_overrides})"
    )


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

    output = "\n".join(
        part.strip() for part in [completed.stdout, completed.stderr] if part and part.strip()
    ).strip()
    raise RuntimeError(
        f"{label} failed with exit code {completed.returncode}"
        + (f": {output}" if output else "")
    )


def _ensure_msgstore_binary() -> None:
    _run_or_raise(["cmake", "--preset", "release"], "cmake configure (release)")
    _run_or_raise(
        ["cmake", "--build", "--preset", "release", "--target", "yahamsgstoreclient"],
        "cmake build (yahamsgstoreclient)",
    )
    if not _MSGSTORE_BINARY.exists():
        raise RuntimeError(f"msgstore binary not found at {_MSGSTORE_BINARY}")


def _write_msgstore_config(
    config_path: Path,
    *,
    mqtt_host: str,
    mqtt_port: int,
    http_port: int,
    persistence_dir: Path,
    subscribe_topic: str,
) -> None:
    config_text = "\n".join([
        "[mqtt]",
        f"host = {mqtt_host}",
        f"port = {mqtt_port}",
        f"clientId = {_unique_client_id('msgstore-19-4-5')}",
        "reconnectDelayMs = 200",
        "keepAliveIntervalMs = 1000",
        "loopSleepMs = 20",
        "",
        "[server]",
        "host = 127.0.0.1",
        f"port = {http_port}",
        "path = /store",
        "",
        "[persist]",
        f"directory = {persistence_dir}",
        "filename = message_store.bak",
        "intervalMs = 500",
        "keepFiles = 2",
        "",
        "[subscriptions]",
        f"{subscribe_topic} = 1",
        "$SYS/# = 1",
        "",
    ])
    config_path.write_text(config_text, encoding="utf-8")


def _start_msgstore_process(config_path: Path, working_directory: Path) -> subprocess.Popen[str]:
    return subprocess.Popen(
        [str(_MSGSTORE_BINARY), str(config_path)],
        cwd=working_directory,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
    )


def _stop_process(process: subprocess.Popen[str] | None) -> None:
    if process is None:
        return
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def _fetch_store_nodes(host: str, port: int, timeout_seconds: float) -> list[dict[str, object]]:
    url = f"http://{host}:{port}/store"
    request = urllib.request.Request(url, method="GET")
    request.add_header("levelamount", "10")
    request.add_header("history", "false")
    request.add_header("reason", "false")
    with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
        payload = response.read().decode("utf-8")
    parsed = json.loads(payload)
    if not isinstance(parsed, list):
        raise RuntimeError("msgstore HTTP response is not a JSON array")
    return parsed


def _wait_for_store_topic_value(
    *,
    host: str,
    port: int,
    topic: str,
    expected_value: str,
    timeout_seconds: float,
) -> bool:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            nodes = _fetch_store_nodes(host, port, timeout_seconds=min(1.0, timeout_seconds))
            for node in nodes:
                if str(node.get("topic", "")) != topic:
                    continue
                value = node.get("value")
                if isinstance(value, str) and value == expected_value:
                    return True
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError):
            pass
        time.sleep(0.2)
    return False


def _wait_for_http_ready(host: str, port: int, timeout_seconds: float) -> bool:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            _fetch_store_nodes(host, port, timeout_seconds=min(1.0, timeout_seconds))
            return True
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError):
            time.sleep(0.2)
    return False


def _new_connect_properties(**values):
    properties = Properties(PacketTypes.CONNECT)
    for key_name, value in values.items():
        setattr(properties, key_name, value)
    return properties


def _persistent_overrides(data_dir: Path, mqtt_port: int) -> dict[str, object]:
    return {
        "network.mqtt_port": mqtt_port,
        "network.ws_port": 0,
        "broker.allow_anonymous": True,
        "persistence.enabled": True,
        "persistence.dir": str(data_dir),
    }


def run_19_4_1_restart_restores_retained_messages(config) -> tuple[bool, str]:
    process = None
    data_dir = Path(tempfile.mkdtemp(prefix="mqtt-integration-19-4-1-"))
    mqtt_port = _find_free_port()
    overrides = _persistent_overrides(data_dir, mqtt_port)
    topic = f"integration/robustness/19-4-1/retained/{uuid.uuid4().hex}"
    payload = b"retained-after-restart"

    try:
        process = start_broker(overrides)
        host = _broker_module.resolve_target_host("127.0.0.1")
        timeout_seconds = max(1.0, float(config.timeout_seconds))

        with MqttClient(timeout_seconds=timeout_seconds) as publisher:
            connack = publisher.connect(host, mqtt_port, client_id=_unique_client_id("pub-19-4-1"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            assert_reason_code(publisher.publish(topic, payload, qos=0, retain=True), 0x00)

        process = restart_broker(process, overrides)

        with MqttClient(timeout_seconds=max(timeout_seconds, 10.0)) as subscriber:
            connack = subscriber.connect(host, mqtt_port, client_id=_unique_client_id("sub-19-4-1"), clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=0)
            if not suback_codes:
                return False, "19.4.1 empty SUBACK after restart"
            assert_reason_code(suback_codes[0], 0x00)
            messages = subscriber.collect_messages(count=1, timeout=max(timeout_seconds, 8.0))
            if messages[0].topic != topic or bytes(messages[0].payload) != payload:
                return False, "19.4.1 retained payload mismatch after restart"

        return True, "19.4.1 retained messages were restored from persistence after broker restart"
    except Exception as error:
        return False, f"19.4.1 failed: {error}"
    finally:
        stop_broker(process)
        shutil.rmtree(data_dir, ignore_errors=True)


def run_19_4_2_restart_restores_sessions_with_expiry(config) -> tuple[bool, str]:
    process = None
    data_dir = Path(tempfile.mkdtemp(prefix="mqtt-integration-19-4-2-"))
    mqtt_port = _find_free_port()
    overrides = _persistent_overrides(data_dir, mqtt_port)
    session_client_id = _unique_client_id("session-19-4-2")
    topic = f"integration/robustness/19-4-2/session/{uuid.uuid4().hex}"
    payload = b"session-persisted-after-restart"

    try:
        process = start_broker(overrides)
        host = _broker_module.resolve_target_host("127.0.0.1")
        timeout_seconds = max(1.0, float(config.timeout_seconds))
        connect_properties = _new_connect_properties(SessionExpiryInterval=_SESSION_EXPIRY_SECONDS)

        with MqttClient(timeout_seconds=timeout_seconds) as subscriber:
            connack = subscriber.connect(
                host,
                mqtt_port,
                client_id=session_client_id,
                clean_start=True,
                properties=connect_properties,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(topic, qos=1)
            if not suback_codes:
                return False, "19.4.2 empty SUBACK while creating session"
            assert_reason_code(suback_codes[0], 0x01)

        process = restart_broker(process, overrides)

        with MqttClient(timeout_seconds=timeout_seconds) as resumed:
            resume_properties = _new_connect_properties(SessionExpiryInterval=_SESSION_EXPIRY_SECONDS)
            connack = resumed.connect(
                host,
                mqtt_port,
                client_id=session_client_id,
                clean_start=False,
                properties=resume_properties,
            )
            assert_connack(connack, reason_code=0x00, session_present=True)

            with MqttClient(timeout_seconds=timeout_seconds) as publisher:
                pub_connack = publisher.connect(
                    host,
                    mqtt_port,
                    client_id=_unique_client_id("pub-19-4-2"),
                    clean_start=True,
                )
                assert_connack(pub_connack, reason_code=0x00, session_present=False)
                assert_reason_code(publisher.publish(topic, payload, qos=1), 0x00)

            messages = resumed.collect_messages(count=1, timeout=max(timeout_seconds, 8.0))
            message = messages[0]
            if message.topic != topic or bytes(message.payload) != payload:
                return False, "19.4.2 resumed session did not receive expected payload after restart"

        return True, "19.4.2 sessions with expiry were restored and resumed after restart"
    except Exception as error:
        return False, f"19.4.2 failed: {error}"
    finally:
        stop_broker(process)
        shutil.rmtree(data_dir, ignore_errors=True)


def run_19_4_3_restart_resumes_inflight_qos1_qos2(config) -> tuple[bool, str]:
    process = None
    data_dir = Path(tempfile.mkdtemp(prefix="mqtt-integration-19-4-3-"))
    mqtt_port = _find_free_port()
    overrides = _persistent_overrides(data_dir, mqtt_port)
    session_client_id = _unique_client_id("session-19-4-3")
    topic_qos1 = f"integration/robustness/19-4-3/qos1/{uuid.uuid4().hex}"
    topic_qos2 = f"integration/robustness/19-4-3/qos2/{uuid.uuid4().hex}"
    payload_qos1 = b"offline-qos1-before-restart"
    payload_qos2 = b"offline-qos2-before-restart"

    try:
        process = start_broker(overrides)
        host = _broker_module.resolve_target_host("127.0.0.1")
        timeout_seconds = max(1.0, float(config.timeout_seconds))
        session_properties = _new_connect_properties(SessionExpiryInterval=_SESSION_EXPIRY_SECONDS)

        with MqttClient(timeout_seconds=timeout_seconds) as subscriber:
            connack = subscriber.connect(
                host,
                mqtt_port,
                client_id=session_client_id,
                clean_start=True,
                properties=session_properties,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_qos1 = subscriber.subscribe(topic_qos1, qos=1)
            suback_qos2 = subscriber.subscribe(topic_qos2, qos=2)
            if not suback_qos1 or not suback_qos2:
                return False, "19.4.3 empty SUBACK while creating QoS1/QoS2 session"
            assert_reason_code(suback_qos1[0], 0x01)
            assert_reason_code(suback_qos2[0], 0x02)

        with MqttClient(timeout_seconds=timeout_seconds) as publisher:
            pub_connack = publisher.connect(host, mqtt_port, client_id=_unique_client_id("pub-19-4-3"), clean_start=True)
            assert_connack(pub_connack, reason_code=0x00, session_present=False)
            assert_reason_code(publisher.publish(topic_qos1, payload_qos1, qos=1), 0x00)
            assert_reason_code(publisher.publish(topic_qos2, payload_qos2, qos=2), 0x00)

        process = restart_broker(process, overrides)

        with MqttClient(timeout_seconds=max(timeout_seconds, 12.0)) as resumed:
            resume_properties = _new_connect_properties(SessionExpiryInterval=_SESSION_EXPIRY_SECONDS)
            connack = resumed.connect(
                host,
                mqtt_port,
                client_id=session_client_id,
                clean_start=False,
                properties=resume_properties,
            )
            assert_connack(connack, reason_code=0x00, session_present=True)
            messages = resumed.collect_messages(count=2, timeout=max(timeout_seconds, 12.0))
            delivered = {(message.topic, bytes(message.payload), int(message.qos)) for message in messages}
            expected = {
                (topic_qos1, payload_qos1, 1),
                (topic_qos2, payload_qos2, 2),
            }
            if delivered != expected:
                return False, f"19.4.3 inflight resume mismatch: expected={expected}, got={delivered}"

        return True, "19.4.3 queued inflight QoS1/QoS2 traffic resumed after restart"
    except Exception as error:
        return False, f"19.4.3 failed: {error}"
    finally:
        stop_broker(process)
        shutil.rmtree(data_dir, ignore_errors=True)


def run_19_4_4_crash_recovery_maintains_data_integrity(config) -> tuple[bool, str]:
    process = None
    data_dir = Path(tempfile.mkdtemp(prefix="mqtt-integration-19-4-4-"))
    mqtt_port = _find_free_port()
    overrides = _persistent_overrides(data_dir, mqtt_port)
    session_client_id = _unique_client_id("session-19-4-4")
    queued_topic = f"integration/robustness/19-4-4/queued/{uuid.uuid4().hex}"
    retained_root = f"integration/robustness/19-4-4/retained/{uuid.uuid4().hex}"
    retained_count = 20
    queued_count = 120

    try:
        process = start_broker(overrides)
        host = _broker_module.resolve_target_host("127.0.0.1")
        timeout_seconds = max(1.0, float(config.timeout_seconds))
        session_properties = _new_connect_properties(SessionExpiryInterval=_SESSION_EXPIRY_SECONDS)

        retained_expected: dict[str, bytes] = {}
        with MqttClient(timeout_seconds=max(timeout_seconds, 12.0)) as publisher:
            pub_connack = publisher.connect(host, mqtt_port, client_id=_unique_client_id("pub-19-4-4"), clean_start=True)
            assert_connack(pub_connack, reason_code=0x00, session_present=False)
            for retained_index in range(retained_count):
                topic = f"{retained_root}/{retained_index}"
                payload = f"retained-{retained_index}".encode("utf-8")
                retained_expected[topic] = payload
                assert_reason_code(publisher.publish(topic, payload, qos=0, retain=True), 0x00)

        with MqttClient(timeout_seconds=timeout_seconds) as subscriber:
            connack = subscriber.connect(
                host,
                mqtt_port,
                client_id=session_client_id,
                clean_start=True,
                properties=session_properties,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = subscriber.subscribe(queued_topic, qos=1)
            if not suback_codes:
                return False, "19.4.4 empty SUBACK while creating queued-session subscription"
            assert_reason_code(suback_codes[0], 0x01)

        with MqttClient(timeout_seconds=max(timeout_seconds, 12.0)) as publisher:
            pub_connack = publisher.connect(
                host,
                mqtt_port,
                client_id=_unique_client_id("pub-19-4-4-queue"),
                clean_start=True,
            )
            assert_connack(pub_connack, reason_code=0x00, session_present=False)
            for queued_index in range(queued_count):
                payload = f"queued-{queued_index}".encode("utf-8")
                assert_reason_code(publisher.publish(queued_topic, payload, qos=1), 0x00)

        if process is not None:
            process.kill()
            process.wait(timeout=5)
            process = None

        process = start_broker(overrides)

        with MqttClient(timeout_seconds=max(timeout_seconds, 12.0)) as retained_probe:
            connack = retained_probe.connect(
                host,
                mqtt_port,
                client_id=_unique_client_id("probe-19-4-4-retained"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback_codes = retained_probe.subscribe(f"{retained_root}/#", qos=0)
            if not suback_codes:
                return False, "19.4.4 empty SUBACK for retained integrity probe"
            assert_reason_code(suback_codes[0], 0x00)
            retained_messages = retained_probe.collect_messages(count=retained_count, timeout=max(timeout_seconds, 12.0))
            retained_delivered = {message.topic: bytes(message.payload) for message in retained_messages}
            if retained_delivered != retained_expected:
                missing = len(set(retained_expected.keys()) - set(retained_delivered.keys()))
                return False, f"19.4.4 retained integrity mismatch after crash recovery, missing={missing}"

        with MqttClient(timeout_seconds=max(timeout_seconds, 16.0)) as resumed:
            resume_properties = _new_connect_properties(SessionExpiryInterval=_SESSION_EXPIRY_SECONDS)
            connack = resumed.connect(
                host,
                mqtt_port,
                client_id=session_client_id,
                clean_start=False,
                properties=resume_properties,
            )
            assert_connack(connack, reason_code=0x00, session_present=True)
            queued_messages = resumed.collect_messages(count=queued_count, timeout=max(timeout_seconds, 20.0))
            delivered_payloads = {bytes(message.payload) for message in queued_messages}
            expected_payloads = {f"queued-{queued_index}".encode("utf-8") for queued_index in range(queued_count)}
            if delivered_payloads != expected_payloads:
                missing = len(expected_payloads - delivered_payloads)
                return False, f"19.4.4 queued data integrity mismatch after crash recovery, missing={missing}"

        return True, "19.4.4 crash-recovery preserved retained and queued-session data integrity"
    except Exception as error:
        return False, f"19.4.4 failed: {error}"
    finally:
        stop_broker(process)
        shutil.rmtree(data_dir, ignore_errors=True)


def run_19_4_5_broker_sigkill_msgstore_survives_and_reconnects(config) -> tuple[bool, str]:
    broker_process = None
    msgstore_process = None
    working_dir = Path(tempfile.mkdtemp(prefix="mqtt-integration-19-4-5-"))
    mqtt_port = _find_free_port()
    http_port = _find_free_port()
    publish_topic = f"integration/robustness/19-4-5/reconnect/{uuid.uuid4().hex}"
    publish_payload = f"after-restart-{uuid.uuid4().hex}"
    msgstore_config_path = working_dir / "msgstore.ini"

    try:
        _require_managed_broker_in_remote(
            "broker process control (SIGKILL), isolated mqtt/http ports, local msgstore process"
        )
        _ensure_msgstore_binary()

        broker_overrides = {
            "network.mqtt_port": mqtt_port,
            "network.ws_port": 0,
            "broker.allow_anonymous": True,
        }
        broker_process = start_broker(broker_overrides)
        host = _broker_module.resolve_target_host("127.0.0.1")

        _write_msgstore_config(
            msgstore_config_path,
            mqtt_host=host,
            mqtt_port=mqtt_port,
            http_port=http_port,
            persistence_dir=working_dir,
            subscribe_topic="integration/robustness/19-4-5/#",
        )
        msgstore_process = _start_msgstore_process(msgstore_config_path, working_dir)
        if not _wait_for_http_ready("127.0.0.1", http_port, timeout_seconds=max(4.0, config.timeout_seconds)):
            return False, "19.4.5 msgstore HTTP endpoint did not become ready"

        if broker_process is None:
            return False, "19.4.5 requires managed local broker process"

        broker_process.kill()
        broker_process.wait(timeout=5)
        broker_process = None

        time.sleep(1.0)
        if msgstore_process.poll() is not None:
            return False, "19.4.5 msgstore crashed after broker SIGKILL"

        broker_process = start_broker(broker_overrides)

        with MqttClient(timeout_seconds=max(2.0, config.timeout_seconds)) as publisher:
            connack = publisher.connect(
                host,
                mqtt_port,
                client_id=_unique_client_id("pub-19-4-5"),
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            assert_reason_code(
                publisher.publish(publish_topic, publish_payload.encode("utf-8"), qos=1),
                0x00,
            )

        if msgstore_process.poll() is not None:
            return False, "19.4.5 msgstore crashed after broker restart"

        observed = _wait_for_store_topic_value(
            host="127.0.0.1",
            port=http_port,
            topic=publish_topic,
            expected_value=publish_payload,
            timeout_seconds=max(6.0, config.timeout_seconds),
        )
        if not observed:
            return False, "19.4.5 msgstore did not store post-restart message (no reconnect evidence)"

        return True, "19.4.5 broker SIGKILL did not crash msgstore; msgstore reconnected and stored post-restart publish"
    except Exception as error:
        return False, f"19.4.5 failed: {error}"
    finally:
        stop_broker(broker_process)
        _stop_process(msgstore_process)
        shutil.rmtree(working_dir, ignore_errors=True)


TEST_CASES = [
    {
        "name": "robustness/recovery_restart_restores_retained_messages",
        "description": "19.4.1 Broker restart -> retained messages restored from persistence",
        "run": run_19_4_1_restart_restores_retained_messages,
    },
    {
        "name": "robustness/recovery_restart_restores_sessions_with_expiry",
        "description": "19.4.2 Broker restart -> sessions with expiry restored",
        "run": run_19_4_2_restart_restores_sessions_with_expiry,
    },
    {
        "name": "robustness/recovery_restart_resumes_inflight_qos",
        "description": "19.4.3 Broker restart -> inflight QoS 1/2 messages resumed",
        "run": run_19_4_3_restart_resumes_inflight_qos1_qos2,
    },
    {
        "name": "robustness/recovery_crash_recovery_data_integrity",
        "description": "19.4.4 Broker crash-recovery -> data integrity maintained",
        "run": run_19_4_4_crash_recovery_maintains_data_integrity,
    },
]
