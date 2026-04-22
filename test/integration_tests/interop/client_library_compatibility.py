"""Integration tests for interoperability section 20.1 (Client Library Compatibility)."""

from __future__ import annotations

import importlib.util
import shutil
import socket
import subprocess
import time
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


_assertions_module = _load_helper("assertions")
_broker_module = _load_helper("broker")
_mqtt_client_module = _load_helper("mqtt_client")

assert_connack = _assertions_module.assert_connack
assert_message = _assertions_module.assert_message
assert_no_message = _assertions_module.assert_no_message
assert_reason_code = _assertions_module.assert_reason_code
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient
PacketTypes = _mqtt_client_module.PacketTypes
Properties = _mqtt_client_module.Properties


def _unique_id(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def _start_isolated_broker(extra_overrides: dict | None = None) -> tuple[str, int, object]:
    overrides: dict = {
        "network.mqtt_port": _find_free_port(),
        "network.ws_port": 0,
        "broker.allow_anonymous": True,
    }
    if extra_overrides:
        overrides.update(extra_overrides)
    process = start_broker(overrides)
    host = _broker_module.resolve_target_host("127.0.0.1")
    return host, int(overrides["network.mqtt_port"]), process


def _find_mqttx() -> str | None:
    test_dir = Path(__file__).resolve().parents[2]
    local_mqttx = test_dir / "mqttx.exe"
    if local_mqttx.exists():
        return str(local_mqttx)
    return shutil.which("mqttx")


def _new_connect_properties(**values):
    properties = Properties(PacketTypes.CONNECT)
    for key, value in values.items():
        setattr(properties, key, value)
    return properties


def run_20_1_1_mqttx_full_session_lifecycle(config) -> tuple[bool, str]:
    """20.1.1 — MQTTX CLI client full session lifecycle.

    Exercises: CONNECT → SUBSCRIBE (background sub) → PUBLISH (via mqttx pub) →
    message delivery → DISCONNECT for both client roles.
    """
    mqttx_path = _find_mqttx()
    if not mqttx_path:
        return False, "mqttx binary not found — install MQTTX CLI and ensure it is in PATH"

    process = None
    sub_proc = None
    host, port, process = _start_isolated_broker()
    topic = f"integration/interop/20-1-1/{uuid.uuid4().hex}"
    payload = "hello-from-mqttx-interop"

    try:
        sub_cmd = [
            mqttx_path, "sub",
            "--hostname", host,
            "--port", str(port),
            "--client-id", _unique_id("mqttx-sub"),
            "--topic", topic,
            "--qos", "1",
            "--output-mode", "clean",
            "--reconnect-period", "0",
        ]
        sub_proc = subprocess.Popen(
            sub_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        # Allow the subscriber to connect and subscribe before publishing.
        time.sleep(2.0)

        pub_cmd = [
            mqttx_path, "pub",
            "--hostname", host,
            "--port", str(port),
            "--client-id", _unique_id("mqttx-pub"),
            "--topic", topic,
            "--message", payload,
            "--qos", "1",
            "--reconnect-period", "0",
        ]
        pub_result = subprocess.run(
            pub_cmd,
            capture_output=True,
            text=True,
            timeout=config.timeout_seconds,
        )
        if pub_result.returncode != 0:
            pub_out = "\n".join(
                p for p in [pub_result.stdout.strip(), pub_result.stderr.strip()] if p
            )
            return False, f"mqttx pub failed (exit={pub_result.returncode}): {pub_out}"

        # Give the subscriber time to receive the message, then read its output.
        time.sleep(1.5)
        sub_proc.terminate()
        try:
            sub_stdout, sub_stderr = sub_proc.communicate(timeout=5.0)
        except subprocess.TimeoutExpired:
            sub_proc.kill()
            sub_stdout, sub_stderr = sub_proc.communicate()

        combined = sub_stdout + sub_stderr
        if payload not in combined:
            return False, (
                f"mqttx sub did not receive expected payload '{payload}'; "
                f"stdout={sub_stdout!r} stderr={sub_stderr!r}"
            )

        return True, f"mqttx pub/sub full lifecycle OK — message '{payload}' delivered"

    except subprocess.TimeoutExpired:
        return False, f"mqttx command timed out after {config.timeout_seconds:.1f}s"
    except Exception as exc:
        return False, str(exc)
    finally:
        if sub_proc is not None and sub_proc.poll() is None:
            sub_proc.kill()
            sub_proc.communicate()
        if process is not None:
            stop_broker(process)


def run_20_1_2_paho_full_session_lifecycle(config) -> tuple[bool, str]:
    """20.1.2 — Paho MQTT Python client full session lifecycle.

    Exercises a complete session lifecycle:
      1. Clean-start connect → subscribe at QoS 1 with session expiry → disconnect.
      2. Another client publishes QoS 1 message while subscriber is offline.
      3. Subscriber reconnects (clean_start=False) → CONNACK must report session_present=True.
      4. Queued QoS 1 message is delivered to the resumed session.
    Also verifies QoS 0 immediate delivery and retained-message delivery on subscribe.
    """
    process = None
    try:
        host, port, process = _start_isolated_broker()
        timeout = config.timeout_seconds
        sub_id = _unique_id("paho-sub")
        pub_id = _unique_id("paho-pub")
        topic = f"integration/interop/20-1-2/{uuid.uuid4().hex}"
        payload_offline = b"queued-while-offline"
        payload_qos0 = b"immediate-qos0"
        retained_payload = b"retained-hello"
        retained_topic = f"integration/interop/20-1-2/retained/{uuid.uuid4().hex}"

        # --- Phase 1: establish persistent subscription ---
        with MqttClient(timeout_seconds=timeout) as sub:
            connack = sub.connect(
                host, port,
                client_id=sub_id,
                clean_start=True,
                properties=_new_connect_properties(SessionExpiryInterval=300),
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            suback = sub.subscribe(topic, qos=1)
            assert_reason_code(suback[0], 1)
        # Subscriber is now offline; session persists in broker.

        # --- Phase 2: publish while subscriber is offline ---
        with MqttClient(timeout_seconds=timeout) as pub:
            connack = pub.connect(host, port, client_id=pub_id, clean_start=True)
            assert_connack(connack, reason_code=0x00, session_present=False)
            pub.publish(topic, payload_offline, qos=1)

        # --- Phase 3: resume session, verify session_present=True, receive queued msg ---
        with MqttClient(timeout_seconds=timeout) as sub:
            connack = sub.connect(
                host, port,
                client_id=sub_id,
                clean_start=False,
                properties=_new_connect_properties(SessionExpiryInterval=300),
            )
            assert_connack(connack, reason_code=0x00, session_present=True)
            msgs = sub.collect_message_for_topic(expected_topic=topic, timeout=timeout)
            assert_message(msgs, topic=topic, payload=payload_offline, qos=1, retain=False)

        # --- Phase 4: QoS 0 immediate delivery roundtrip ---
        topic_qos0 = f"integration/interop/20-1-2/qos0/{uuid.uuid4().hex}"
        with MqttClient(timeout_seconds=timeout) as sub:
            sub.connect(host, port, client_id=_unique_id("paho-sub-q0"), clean_start=True)
            sub.subscribe(topic_qos0, qos=0)
            with MqttClient(timeout_seconds=timeout) as pub:
                pub.connect(host, port, client_id=_unique_id("paho-pub-q0"), clean_start=True)
                pub.publish(topic_qos0, payload_qos0, qos=0)
            msgs = sub.collect_message_for_topic(expected_topic=topic_qos0, timeout=timeout)
            assert_message(msgs, topic=topic_qos0, payload=payload_qos0, qos=0, retain=False)

        # --- Phase 5: retained message delivered on fresh subscribe ---
        with MqttClient(timeout_seconds=timeout) as pub:
            pub.connect(host, port, client_id=_unique_id("paho-retain-pub"), clean_start=True)
            pub.publish(retained_topic, retained_payload, qos=1, retain=True)
        with MqttClient(timeout_seconds=timeout) as sub:
            sub.connect(host, port, client_id=_unique_id("paho-retain-sub"), clean_start=True)
            sub.subscribe(retained_topic, qos=1)
            msgs = sub.collect_message_for_topic(expected_topic=retained_topic, timeout=timeout)
            assert_message(msgs, topic=retained_topic, payload=retained_payload, qos=1, retain=False)

        return True, "Paho full session lifecycle OK (session resume, QoS 0/1 delivery, retained)"

    except Exception as exc:
        return False, str(exc)
    finally:
        if process is not None:
            stop_broker(process)


def _find_mosquitto_pub() -> str | None:
    return shutil.which("mosquitto_pub")


def _find_mosquitto_sub() -> str | None:
    return shutil.which("mosquitto_sub")


def run_20_1_3_mosquitto_basic_pub_sub(config) -> tuple[bool, str]:
    """20.1.3 — Mosquitto client tools basic pub/sub.

    Exercises:
      1. mosquitto_pub: CONNECT → PUBLISH QoS 0 → DISCONNECT
      2. mosquitto_sub: CONNECT → SUBSCRIBE → receive exactly one message → exit
         (-C 1 causes mosquitto_sub to exit after the first message)
    """
    mosquitto_pub = _find_mosquitto_pub()
    mosquitto_sub = _find_mosquitto_sub()
    if not mosquitto_pub:
        return False, "mosquitto_pub not found — install mosquitto client tools"
    if not mosquitto_sub:
        return False, "mosquitto_sub not found — install mosquitto client tools"

    process = None
    sub_proc = None
    try:
        host, port, process = _start_isolated_broker()
        topic = f"integration/interop/20-1-3/{uuid.uuid4().hex}"
        payload = "hello-from-mosquitto"

        sub_cmd = [
            mosquitto_sub,
            "-h", host,
            "-p", str(port),
            "-i", _unique_id("mosq-sub"),
            "-t", topic,
            "-q", "0",
            "-V", "5",            # force MQTT 5.0 — broker does not support v3.1.1
            "-C", "1",            # exit after first message
            "-W", str(int(config.timeout_seconds)),
        ]
        sub_proc = subprocess.Popen(
            sub_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        # Allow subscriber to connect before publishing.
        time.sleep(1.5)

        pub_cmd = [
            mosquitto_pub,
            "-h", host,
            "-p", str(port),
            "-i", _unique_id("mosq-pub"),
            "-t", topic,
            "-m", payload,
            "-q", "0",
            "-V", "5",            # force MQTT 5.0 — broker does not support v3.1.1
        ]
        pub_result = subprocess.run(
            pub_cmd,
            capture_output=True,
            text=True,
            timeout=config.timeout_seconds,
        )
        if pub_result.returncode != 0:
            pub_out = "\n".join(
                p for p in [pub_result.stdout.strip(), pub_result.stderr.strip()] if p
            )
            return False, f"mosquitto_pub failed (exit={pub_result.returncode}): {pub_out}"

        # mosquitto_sub exits on its own after -C 1; wait for it.
        try:
            sub_stdout, sub_stderr = sub_proc.communicate(timeout=config.timeout_seconds)
        except subprocess.TimeoutExpired:
            sub_proc.kill()
            sub_stdout, sub_stderr = sub_proc.communicate()
            return False, f"mosquitto_sub timed out waiting for message"

        if sub_proc.returncode != 0:
            return False, (
                f"mosquitto_sub exited with code {sub_proc.returncode}: "
                f"stdout={sub_stdout!r} stderr={sub_stderr!r}"
            )

        received = sub_stdout.strip()
        if payload not in received:
            return False, (
                f"mosquitto_sub did not receive expected payload '{payload}'; "
                f"got: {received!r}"
            )

        return True, f"mosquitto pub/sub basic roundtrip OK — received '{received}'"

    except subprocess.TimeoutExpired:
        return False, f"mosquitto command timed out after {config.timeout_seconds:.1f}s"
    except Exception as exc:
        return False, str(exc)
    finally:
        if sub_proc is not None and sub_proc.poll() is None:
            sub_proc.kill()
            sub_proc.communicate()
        if process is not None:
            stop_broker(process)


TEST_CASES = [
    {
        "name": "interop/20_1_1_mqttx_full_session_lifecycle",
        "description": "20.1.1 MQTTX CLI client — full session lifecycle",
        "run": run_20_1_1_mqttx_full_session_lifecycle,
    },
    {
        "name": "interop/20_1_2_paho_full_session_lifecycle",
        "description": "20.1.2 Paho MQTT Python client — full session lifecycle",
        "run": run_20_1_2_paho_full_session_lifecycle,
    },
    {
        "name": "interop/20_1_3_mosquitto_basic_pub_sub",
        "description": "20.1.3 Mosquitto client tools — basic pub/sub",
        "run": run_20_1_3_mosquitto_basic_pub_sub,
    },
]
