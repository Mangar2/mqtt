from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
import uuid


def _load_helper(module_name: str):
    helper_path = Path(__file__).resolve().parents[2] / "integration_tests" / "helpers" / f"{module_name}.py"
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
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
MqttClient = _mqtt_client_module.MqttClient

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
_RELEASE_DIR = _PROJECT_ROOT / "build" / "release"
_MSGSTORE_BINARY = _RELEASE_DIR / ("yahamsgstoreclient.exe" if os.name == "nt" else "yahamsgstoreclient")


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
        f"clientId = yaha-msgstore-test-{uuid.uuid4().hex[:8]}",
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


def _wait_for_http_ready(host: str, port: int, timeout_seconds: float) -> bool:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            _fetch_store_nodes(host, port, timeout_seconds=min(1.0, timeout_seconds))
            return True
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError):
            time.sleep(0.2)
    return False


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


def _publish_text_message(
    *,
    host: str,
    port: int,
    topic: str,
    payload_text: str,
    timeout_seconds: float,
) -> None:
    with MqttClient(timeout_seconds=max(2.0, timeout_seconds)) as publisher:
        connack = publisher.connect(
            host,
            port,
            client_id=f"pub-{uuid.uuid4().hex[:10]}",
            clean_start=True,
        )
        assert_connack(connack, reason_code=0x00, session_present=False)
        reason_code = int(publisher.publish(topic, payload_text.encode("utf-8"), qos=1))
        if reason_code not in (0x00, 0x10):
            raise RuntimeError(f"unexpected PUBACK reason: 0x{reason_code:02X}")


def _make_runtime(
    *,
    config,
    subscribe_topic: str,
    persist_interval_ms: int = 500,
) -> tuple[Path, int, int, str, Path, object, subprocess.Popen[str] | None]:
    working_dir = Path(tempfile.mkdtemp(prefix="mqtt-yaha-msgstore-it-"))
    mqtt_port = _find_free_port()
    http_port = _find_free_port()
    msgstore_config_path = working_dir / "msgstore.ini"

    broker_overrides = {
        "network.mqtt_port": mqtt_port,
        "network.ws_port": 0,
        "broker.allow_anonymous": True,
    }
    broker_process = start_broker(broker_overrides)
    if broker_process is None:
        raise RuntimeError("test requires managed local broker process")

    host = _broker_module.resolve_target_host("127.0.0.1")
    _write_msgstore_config(
        msgstore_config_path,
        mqtt_host=host,
        mqtt_port=mqtt_port,
        http_port=http_port,
        persistence_dir=working_dir,
        subscribe_topic=subscribe_topic,
    )
    raw_text = msgstore_config_path.read_text(encoding="utf-8")
    raw_text = raw_text.replace("intervalMs = 500", f"intervalMs = {persist_interval_ms}")
    msgstore_config_path.write_text(raw_text, encoding="utf-8")

    msgstore_process = _start_msgstore_process(msgstore_config_path, working_dir)
    ready = _wait_for_http_ready("127.0.0.1", http_port, timeout_seconds=max(4.0, config.timeout_seconds))
    if not ready:
        raise RuntimeError("msgstore HTTP endpoint did not become ready")

    return working_dir, mqtt_port, http_port, host, msgstore_config_path, broker_process, msgstore_process


def _cleanup_runtime(working_dir: Path, broker_process, msgstore_process: subprocess.Popen[str] | None) -> None:
    stop_broker(broker_process)
    _stop_process(msgstore_process)
    shutil.rmtree(working_dir, ignore_errors=True)


def run_msgstore_stores_and_serves_latest_value(config) -> tuple[bool, str]:
    working_dir = Path(".")
    broker_process = None
    msgstore_process = None
    try:
        _ensure_msgstore_binary()
        runtime = _make_runtime(
            config=config,
            subscribe_topic="integration/yaha/msgstore/roundtrip/#",
            persist_interval_ms=0,
        )
        working_dir, mqtt_port, http_port, host, _config_path, broker_process, msgstore_process = runtime

        topic_name = f"integration/yaha/msgstore/roundtrip/{uuid.uuid4().hex}"
        payload_text = f"value-{uuid.uuid4().hex}"
        _publish_text_message(
            host=host,
            port=mqtt_port,
            topic=topic_name,
            payload_text=payload_text,
            timeout_seconds=config.timeout_seconds,
        )

        observed = _wait_for_store_topic_value(
            host="127.0.0.1",
            port=http_port,
            topic=topic_name,
            expected_value=payload_text,
            timeout_seconds=max(6.0, config.timeout_seconds),
        )
        if not observed:
            return False, "msgstore did not expose the published topic/value via HTTP"

        return True, "msgstore stored published message and served it via HTTP"
    except Exception as error:
        return False, f"msgstore_roundtrip failed: {error}"
    finally:
        _cleanup_runtime(working_dir, broker_process, msgstore_process)


def run_msgstore_respects_subscription_scope(config) -> tuple[bool, str]:
    working_dir = Path(".")
    broker_process = None
    msgstore_process = None
    try:
        _ensure_msgstore_binary()
        runtime = _make_runtime(
            config=config,
            subscribe_topic="integration/yaha/msgstore/allowed/#",
            persist_interval_ms=0,
        )
        working_dir, mqtt_port, http_port, host, _config_path, broker_process, msgstore_process = runtime

        allowed_topic = f"integration/yaha/msgstore/allowed/{uuid.uuid4().hex}"
        blocked_topic = f"integration/yaha/msgstore/blocked/{uuid.uuid4().hex}"
        allowed_payload = f"allowed-{uuid.uuid4().hex}"
        blocked_payload = f"blocked-{uuid.uuid4().hex}"

        _publish_text_message(
            host=host,
            port=mqtt_port,
            topic=allowed_topic,
            payload_text=allowed_payload,
            timeout_seconds=config.timeout_seconds,
        )
        _publish_text_message(
            host=host,
            port=mqtt_port,
            topic=blocked_topic,
            payload_text=blocked_payload,
            timeout_seconds=config.timeout_seconds,
        )

        allowed_observed = _wait_for_store_topic_value(
            host="127.0.0.1",
            port=http_port,
            topic=allowed_topic,
            expected_value=allowed_payload,
            timeout_seconds=max(6.0, config.timeout_seconds),
        )
        if not allowed_observed:
            return False, "msgstore did not store message for configured subscription topic"

        blocked_observed = _wait_for_store_topic_value(
            host="127.0.0.1",
            port=http_port,
            topic=blocked_topic,
            expected_value=blocked_payload,
            timeout_seconds=2.0,
        )
        if blocked_observed:
            return False, "msgstore stored message outside configured subscription scope"

        return True, "msgstore stored only subscribed topics and ignored out-of-scope topics"
    except Exception as error:
        return False, f"msgstore_subscription_scope failed: {error}"
    finally:
        _cleanup_runtime(working_dir, broker_process, msgstore_process)


def run_msgstore_restores_persisted_state_after_restart(config) -> tuple[bool, str]:
    working_dir = Path(".")
    broker_process = None
    msgstore_process = None
    try:
        _ensure_msgstore_binary()
        runtime = _make_runtime(
            config=config,
            subscribe_topic="integration/yaha/msgstore/persist/#",
            persist_interval_ms=150,
        )
        working_dir, mqtt_port, http_port, host, config_path, broker_process, msgstore_process = runtime

        topic_name = f"integration/yaha/msgstore/persist/{uuid.uuid4().hex}"
        payload_text = f"persisted-{uuid.uuid4().hex}"
        _publish_text_message(
            host=host,
            port=mqtt_port,
            topic=topic_name,
            payload_text=payload_text,
            timeout_seconds=config.timeout_seconds,
        )

        before_restart_observed = _wait_for_store_topic_value(
            host="127.0.0.1",
            port=http_port,
            topic=topic_name,
            expected_value=payload_text,
            timeout_seconds=max(6.0, config.timeout_seconds),
        )
        if not before_restart_observed:
            return False, "msgstore did not store value before restart"

        _stop_process(msgstore_process)
        msgstore_process = _start_msgstore_process(config_path, working_dir)
        if not _wait_for_http_ready("127.0.0.1", http_port, timeout_seconds=max(4.0, config.timeout_seconds)):
            return False, "msgstore HTTP endpoint did not become ready after restart"

        after_restart_observed = _wait_for_store_topic_value(
            host="127.0.0.1",
            port=http_port,
            topic=topic_name,
            expected_value=payload_text,
            timeout_seconds=max(6.0, config.timeout_seconds),
        )
        if not after_restart_observed:
            return False, "msgstore did not restore persisted value after restart"

        return True, "msgstore restored persisted state after process restart"
    except Exception as error:
        return False, f"msgstore_persist_restore failed: {error}"
    finally:
        _cleanup_runtime(working_dir, broker_process, msgstore_process)


def run_msgstore_survives_broker_sigkill_and_reconnects(config) -> tuple[bool, str]:
    broker_process = None
    msgstore_process = None
    working_dir = Path(tempfile.mkdtemp(prefix="mqtt-yaha-msgstore-it-"))
    mqtt_port = _find_free_port()
    http_port = _find_free_port()
    baseline_topic = f"integration/yaha/msgstore/reconnect/baseline/{uuid.uuid4().hex}"
    baseline_payload = f"before-kill-{uuid.uuid4().hex}"
    publish_topic = f"integration/yaha/msgstore/reconnect/{uuid.uuid4().hex}"
    publish_payload = f"after-restart-{uuid.uuid4().hex}"
    msgstore_config_path = working_dir / "msgstore.ini"

    try:
        _ensure_msgstore_binary()

        broker_overrides = {
            "network.mqtt_port": mqtt_port,
            "network.ws_port": 0,
            "broker.allow_anonymous": True,
        }
        broker_process = start_broker(broker_overrides)
        if broker_process is None:
            return False, "test requires managed local broker process"

        host = _broker_module.resolve_target_host("127.0.0.1")

        _write_msgstore_config(
            msgstore_config_path,
            mqtt_host=host,
            mqtt_port=mqtt_port,
            http_port=http_port,
            persistence_dir=working_dir,
            subscribe_topic="integration/yaha/msgstore/reconnect/#",
        )
        msgstore_process = _start_msgstore_process(msgstore_config_path, working_dir)
        if not _wait_for_http_ready("127.0.0.1", http_port, timeout_seconds=max(4.0, config.timeout_seconds)):
            return False, "msgstore HTTP endpoint did not become ready"

        # Baseline: prove msgstore currently receives broker traffic before SIGKILL.
        baseline_observed = False
        baseline_deadline = time.monotonic() + max(10.0, config.timeout_seconds)
        with MqttClient(timeout_seconds=max(2.0, config.timeout_seconds)) as baseline_publisher:
            connack = baseline_publisher.connect(
                host,
                mqtt_port,
                client_id=f"baseline-pub-{uuid.uuid4().hex[:10]}",
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)
            while time.monotonic() < baseline_deadline:
                baseline_reason = int(
                    baseline_publisher.publish(
                        baseline_topic,
                        baseline_payload.encode("utf-8"),
                        qos=1,
                    )
                )
                if baseline_reason not in (0x00, 0x10):
                    return False, f"unexpected baseline PUBACK reason: 0x{baseline_reason:02X}"

                if _wait_for_store_topic_value(
                    host="127.0.0.1",
                    port=http_port,
                    topic=baseline_topic,
                    expected_value=baseline_payload,
                    timeout_seconds=1.0,
                ):
                    baseline_observed = True
                    break
                time.sleep(0.2)

        if not baseline_observed:
            return False, "msgstore baseline receive check failed before broker SIGKILL"

        broker_process.kill()
        broker_process.wait(timeout=5)
        broker_process = None

        time.sleep(1.0)
        if msgstore_process.poll() is not None:
            return False, "msgstore crashed after broker SIGKILL"

        broker_process = start_broker(broker_overrides)
        if broker_process is None:
            return False, "broker restart did not return managed process"

        deadline = time.monotonic() + max(8.0, config.timeout_seconds)
        observed = False
        with MqttClient(timeout_seconds=max(2.0, config.timeout_seconds)) as publisher:
            connack = publisher.connect(
                host,
                mqtt_port,
                client_id=f"pub-{uuid.uuid4().hex[:10]}",
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)

            while time.monotonic() < deadline:
                publish_reason = int(
                    publisher.publish(publish_topic, publish_payload.encode("utf-8"), qos=1)
                )
                if publish_reason not in (0x00, 0x10):
                    return False, f"unexpected PUBACK reason after restart: 0x{publish_reason:02X}"

                if _wait_for_store_topic_value(
                    host="127.0.0.1",
                    port=http_port,
                    topic=publish_topic,
                    expected_value=publish_payload,
                    timeout_seconds=1.0,
                ):
                    observed = True
                    break
                time.sleep(0.2)

        if msgstore_process.poll() is not None:
            return False, "msgstore crashed after broker restart"
        if not observed:
            return False, "msgstore did not store post-restart message (no reconnect evidence)"

        return True, "msgstore survived broker SIGKILL and reconnected automatically"
    except Exception as error:
        return False, f"msgstore_reconnect failed: {error}"
    finally:
        stop_broker(broker_process)
        _stop_process(msgstore_process)
        shutil.rmtree(working_dir, ignore_errors=True)


TEST_CASES = [
    {
        "name": "yaha/msgstore/persist_restore_after_restart",
        "description": "Msgstore restores persisted state after process restart",
        "run": run_msgstore_restores_persisted_state_after_restart,
    },
    {
        "name": "yaha/msgstore/roundtrip_store_and_query",
        "description": "Msgstore stores MQTT publish and serves it over HTTP",
        "run": run_msgstore_stores_and_serves_latest_value,
    },
    {
        "name": "yaha/msgstore/reconnect_after_broker_sigkill",
        "description": "Msgstore survives broker SIGKILL and reconnects automatically",
        "run": run_msgstore_survives_broker_sigkill_and_reconnects,
    },
    {
        "name": "yaha/msgstore/subscription_scope_filters_topics",
        "description": "Msgstore stores only topics matching configured subscriptions",
        "run": run_msgstore_respects_subscription_scope,
    },
]
