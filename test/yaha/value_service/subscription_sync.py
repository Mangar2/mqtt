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
_FILESTORE_BINARY = _RELEASE_DIR / ("yahafilestoreclient.exe" if os.name == "nt" else "yahafilestoreclient")
_VALUESERVICE_BINARY = _RELEASE_DIR / ("yahavalueserviceclient.exe" if os.name == "nt" else "yahavalueserviceclient")


class RuntimeContext:
    def __init__(
        self,
        *,
        working_dir: Path,
        mqtt_port: int,
        filestore_port: int,
        broker_host: str,
        base_prefix: str,
        values_key_path: str,
        monitor_prefix: str,
        broker_process,
        filestore_process: subprocess.Popen[str] | None,
        valueservice_process: subprocess.Popen[str] | None,
    ) -> None:
        self.working_dir = working_dir
        self.mqtt_port = mqtt_port
        self.filestore_port = filestore_port
        self.broker_host = broker_host
        self.base_prefix = base_prefix
        self.values_key_path = values_key_path
        self.monitor_prefix = monitor_prefix
        self.broker_process = broker_process
        self.filestore_process = filestore_process
        self.valueservice_process = valueservice_process


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _make_topic_root(name: str) -> str:
    return f"integration/yaha/value_service/{name}/{uuid.uuid4().hex}"


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


def _ensure_binaries() -> None:
    _run_or_raise(["cmake", "--preset", "release"], "cmake configure (release)")
    _run_or_raise(
        ["cmake", "--build", "--preset", "release", "--target", "yahafilestoreclient"],
        "cmake build (yahafilestoreclient)",
    )
    _run_or_raise(
        ["cmake", "--build", "--preset", "release", "--target", "yahavalueserviceclient"],
        "cmake build (yahavalueserviceclient)",
    )
    if not _FILESTORE_BINARY.exists():
        raise RuntimeError(f"filestore binary not found at {_FILESTORE_BINARY}")
    if not _VALUESERVICE_BINARY.exists():
        raise RuntimeError(f"valueservice binary not found at {_VALUESERVICE_BINARY}")


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


def _write_filestore_config(
    config_path: Path,
    *,
    mqtt_host: str,
    mqtt_port: int,
    server_port: int,
    data_dir: Path,
    monitor_prefix: str,
) -> None:
    config_text = "\n".join([
        "[mqtt]",
        f"host = {mqtt_host}",
        f"port = {mqtt_port}",
        f"clientId = yaha-filestore-it-{uuid.uuid4().hex[:8]}",
        "reconnectDelayMs = 200",
        "keepAliveIntervalMs = 1000",
        "loopSleepMs = 20",
        "enableLifecycleTrace = false",
        "enableMessageTrace = false",
        "",
        "[server]",
        "host = 127.0.0.1",
        f"port = {server_port}",
        "",
        "[filestore]",
        f"directory = {data_dir}",
        "keepFiles = 2",
        "maxKeyLength = 255",
        "",
        "[monitoring]",
        "enabled = true",
        f"topicPrefix = {monitor_prefix}",
        "qos = 1",
        "retain = false",
        "watchIntervalMs = 50",
        "",
    ])
    config_path.write_text(config_text, encoding="utf-8")


def _write_valueservice_config(
    config_path: Path,
    *,
    mqtt_host: str,
    mqtt_port: int,
    filestore_port: int,
    values_key_path: str,
    monitor_prefix: str,
) -> None:
    config_text = "\n".join([
        "[mqtt]",
        f"host = {mqtt_host}",
        f"port = {mqtt_port}",
        f"clientId = yaha-valueservice-it-{uuid.uuid4().hex[:8]}",
        "reconnectDelayMs = 200",
        "keepAliveIntervalMs = 1000",
        "loopSleepMs = 20",
        "",
        "[filestore]",
        "use = true",
        "host = 127.0.0.1",
        f"port = {filestore_port}",
        f"filename = {values_key_path}",
        f"topicPrefix = {monitor_prefix}",
        "",
        "[valueservice]",
        "subscribeQoS = 1",
        "",
    ])
    config_path.write_text(config_text, encoding="utf-8")


def _start_process(binary_path: Path, config_path: Path, working_dir: Path) -> subprocess.Popen[str]:
    return subprocess.Popen(
        [str(binary_path), str(config_path)],
        cwd=working_dir,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
    )


def _filestore_post_json(*, host: str, port: int, key_path: str, payload: dict[str, object], timeout: float) -> None:
    url = f"http://{host}:{port}{key_path}"
    body_bytes = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    request = urllib.request.Request(url, data=body_bytes, method="POST")
    request.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(request, timeout=timeout) as response:
        status = int(getattr(response, "status", 0))
        if status != 200:
            raise RuntimeError(f"filestore POST unexpected status {status}")


def _wait_for_filestore_ready(*, host: str, port: int, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            request = urllib.request.Request(f"http://{host}:{port}/healthz", method="GET")
            with urllib.request.urlopen(request, timeout=0.8):
                pass
            return True
        except urllib.error.HTTPError as error:
            if int(error.code) in (400, 404):
                return True
        except (urllib.error.URLError, TimeoutError):
            time.sleep(0.2)
    return False


def _publish_text_message(*, host: str, port: int, topic: str, payload_text: str, timeout_seconds: float) -> None:
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


def _drain_messages(client: MqttClient) -> None:
    while True:
        try:
            client.collect_messages(count=1, timeout=0.05)
        except TimeoutError:
            return


def _wait_for_topic_payload(
    *,
    client: MqttClient,
    topic: str,
    payload_text: str,
    timeout_seconds: float,
) -> bool:
    expected_payload = payload_text.encode("utf-8")
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        remaining = max(0.05, min(0.3, deadline - time.monotonic()))
        try:
            messages = client.collect_messages(count=1, timeout=remaining)
        except TimeoutError:
            continue

        for message in messages:
            if message.topic == topic and message.payload == expected_payload:
                return True

    return False


def _assert_set_roundtrip(
    *,
    client: MqttClient,
    host: str,
    port: int,
    key_topic: str,
    payload_text: str,
    timeout_seconds: float,
) -> None:
    _drain_messages(client)
    _publish_text_message(
        host=host,
        port=port,
        topic=f"{key_topic}/set",
        payload_text=payload_text,
        timeout_seconds=timeout_seconds,
    )
    if not _wait_for_topic_payload(
        client=client,
        topic=key_topic,
        payload_text=payload_text,
        timeout_seconds=max(1.0, timeout_seconds),
    ):
        raise RuntimeError(f"expected response on subscribed key topic {key_topic}")


def _assert_set_ignored(
    *,
    client: MqttClient,
    host: str,
    port: int,
    key_topic: str,
    payload_text: str,
    timeout_seconds: float,
) -> None:
    _drain_messages(client)
    _publish_text_message(
        host=host,
        port=port,
        topic=f"{key_topic}/set",
        payload_text=payload_text,
        timeout_seconds=timeout_seconds,
    )
    if _wait_for_topic_payload(
        client=client,
        topic=key_topic,
        payload_text=payload_text,
        timeout_seconds=max(0.8, timeout_seconds),
    ):
        raise RuntimeError(f"unexpected response on unsubscribed key topic {key_topic}")


def _wait_for_set_roundtrip(
    *,
    client: MqttClient,
    host: str,
    port: int,
    key_topic: str,
    payload_text: str,
    timeout_seconds: float,
) -> bool:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            _assert_set_roundtrip(
                client=client,
                host=host,
                port=port,
                key_topic=key_topic,
                payload_text=payload_text,
                timeout_seconds=1.0,
            )
            return True
        except Exception:
            time.sleep(0.2)
    return False


def _make_runtime(
    *,
    config,
    base_prefix: str,
    initial_values: dict[str, object],
) -> RuntimeContext:
    working_dir = Path(tempfile.mkdtemp(prefix="mqtt-yaha-valueservice-it-"))
    data_dir = working_dir / "data"
    data_dir.mkdir(parents=True, exist_ok=True)

    mqtt_port = _find_free_port()
    filestore_port = _find_free_port()
    run_id = uuid.uuid4().hex
    values_key_path = f"/valueservice/values/{run_id}"
    monitor_prefix = f"$MONITOR/FileStore/{run_id}"

    broker_overrides = {
        "network.mqtt_port": mqtt_port,
        "network.ws_port": 0,
        "broker.allow_anonymous": True,
    }
    broker_process = start_broker(broker_overrides)
    if broker_process is None:
        raise RuntimeError("test requires managed local broker process")

    broker_host = _broker_module.resolve_target_host("127.0.0.1")

    filestore_config = working_dir / "filestore.ini"
    valueservice_config = working_dir / "valueservice.ini"

    _write_filestore_config(
        filestore_config,
        mqtt_host=broker_host,
        mqtt_port=mqtt_port,
        server_port=filestore_port,
        data_dir=data_dir,
        monitor_prefix=monitor_prefix,
    )
    _write_valueservice_config(
        valueservice_config,
        mqtt_host=broker_host,
        mqtt_port=mqtt_port,
        filestore_port=filestore_port,
        values_key_path=values_key_path,
        monitor_prefix=monitor_prefix,
    )

    filestore_process = _start_process(_FILESTORE_BINARY, filestore_config, working_dir)
    if not _wait_for_filestore_ready(host="127.0.0.1", port=filestore_port, timeout=max(4.0, config.timeout_seconds)):
        raise RuntimeError("filestore HTTP endpoint did not become ready")

    _filestore_post_json(
        host="127.0.0.1",
        port=filestore_port,
        key_path=values_key_path,
        payload=initial_values,
        timeout=max(2.0, config.timeout_seconds),
    )

    valueservice_process = _start_process(_VALUESERVICE_BINARY, valueservice_config, working_dir)

    return RuntimeContext(
        working_dir=working_dir,
        mqtt_port=mqtt_port,
        filestore_port=filestore_port,
        broker_host=broker_host,
        base_prefix=base_prefix,
        values_key_path=values_key_path,
        monitor_prefix=monitor_prefix,
        broker_process=broker_process,
        filestore_process=filestore_process,
        valueservice_process=valueservice_process,
    )


def _cleanup_runtime(runtime: RuntimeContext | None) -> None:
    if runtime is None:
        return
    stop_broker(runtime.broker_process)
    _stop_process(runtime.valueservice_process)
    _stop_process(runtime.filestore_process)
    shutil.rmtree(runtime.working_dir, ignore_errors=True)


def _make_key_map(base_prefix: str, suffixes: list[str]) -> dict[str, object]:
    return {f"{base_prefix}/{suffix}": f"init-{suffix}" for suffix in suffixes}


def _with_subscriber(runtime: RuntimeContext, config):
    subscriber = MqttClient(timeout_seconds=max(2.0, config.timeout_seconds))
    connack = subscriber.connect(
        runtime.broker_host,
        runtime.mqtt_port,
        client_id=f"sub-{uuid.uuid4().hex[:10]}",
        clean_start=True,
    )
    assert_connack(connack, reason_code=0x00, session_present=False)
    subscriber.subscribe(f"{runtime.base_prefix}/#", qos=1)
    _drain_messages(subscriber)
    return subscriber


def run_valueservice_startup_subscriptions_match_initial_keys(config) -> tuple[bool, str]:
    runtime = None
    subscriber = None
    try:
        _ensure_binaries()
        base_prefix = _make_topic_root("startup")
        runtime = _make_runtime(
            config=config,
            base_prefix=base_prefix,
            initial_values=_make_key_map(base_prefix, ["alpha", "beta"]),
        )
        subscriber = _with_subscriber(runtime, config)

        key_alpha = f"{base_prefix}/alpha"
        key_beta = f"{base_prefix}/beta"
        key_missing = f"{base_prefix}/gamma"

        if not _wait_for_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=key_alpha,
            payload_text="s1",
            timeout_seconds=max(4.0, config.timeout_seconds),
        ):
            return False, "startup key alpha was not subscribed"

        _assert_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=key_beta,
            payload_text="s2",
            timeout_seconds=max(2.0, config.timeout_seconds),
        )
        _assert_set_ignored(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=key_missing,
            payload_text="nope",
            timeout_seconds=1.0,
        )
        return True, "startup subscriptions match initial key set"
    except Exception as error:
        return False, f"valueservice_startup_subscriptions failed: {error}"
    finally:
        if subscriber is not None:
            subscriber.disconnect()
        _cleanup_runtime(runtime)


def run_valueservice_reload_replace_keys_updates_subscriptions(config) -> tuple[bool, str]:
    runtime = None
    subscriber = None
    try:
        _ensure_binaries()
        base_prefix = _make_topic_root("replace")
        runtime = _make_runtime(
            config=config,
            base_prefix=base_prefix,
            initial_values={
                f"{base_prefix}/old_a": "1",
                f"{base_prefix}/old_b": "2",
            },
        )
        subscriber = _with_subscriber(runtime, config)

        if not _wait_for_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/old_a",
            payload_text="old-ready",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "startup baseline old_a was not subscribed before replace reload"

        _filestore_post_json(
            host="127.0.0.1",
            port=runtime.filestore_port,
            key_path=runtime.values_key_path,
            payload={
                f"{base_prefix}/new_c": "3",
                f"{base_prefix}/new_d": "4",
            },
            timeout=max(2.0, config.timeout_seconds),
        )
        if not _wait_for_topic_payload(
            client=subscriber,
            topic=f"{base_prefix}/new_c",
            payload_text="3",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "replace reload snapshot for new_c was not observed"

        if not _wait_for_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/new_c",
            payload_text="c-now",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "new key new_c not active after replace reload"

        _assert_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/new_d",
            payload_text="d-now",
            timeout_seconds=2.0,
        )
        _assert_set_ignored(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/old_a",
            payload_text="old",
            timeout_seconds=1.0,
        )
        _assert_set_ignored(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/old_b",
            payload_text="old",
            timeout_seconds=1.0,
        )
        return True, "replace reload removed old subscriptions and added new subscriptions"
    except Exception as error:
        return False, f"valueservice_reload_replace failed: {error}"
    finally:
        if subscriber is not None:
            subscriber.disconnect()
        _cleanup_runtime(runtime)


def run_valueservice_reload_add_key_adds_subscription(config) -> tuple[bool, str]:
    runtime = None
    subscriber = None
    try:
        _ensure_binaries()
        base_prefix = _make_topic_root("add")
        runtime = _make_runtime(
            config=config,
            base_prefix=base_prefix,
            initial_values={f"{base_prefix}/base": "1"},
        )
        subscriber = _with_subscriber(runtime, config)

        if not _wait_for_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/base",
            payload_text="base-ready",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "startup baseline base was not subscribed before add reload"

        _filestore_post_json(
            host="127.0.0.1",
            port=runtime.filestore_port,
            key_path=runtime.values_key_path,
            payload={
                f"{base_prefix}/base": "1",
                f"{base_prefix}/new": "2",
            },
            timeout=max(2.0, config.timeout_seconds),
        )
        if not _wait_for_topic_payload(
            client=subscriber,
            topic=f"{base_prefix}/new",
            payload_text="2",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "add reload snapshot for new key was not observed"

        if not _wait_for_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/new",
            payload_text="new-active",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "new key was not subscribed after add reload"

        _assert_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/base",
            payload_text="base-still",
            timeout_seconds=2.0,
        )
        return True, "add reload added missing subscription and preserved existing subscriptions"
    except Exception as error:
        return False, f"valueservice_reload_add failed: {error}"
    finally:
        if subscriber is not None:
            subscriber.disconnect()
        _cleanup_runtime(runtime)


def run_valueservice_reload_remove_key_removes_subscription(config) -> tuple[bool, str]:
    runtime = None
    subscriber = None
    try:
        _ensure_binaries()
        base_prefix = _make_topic_root("remove")
        runtime = _make_runtime(
            config=config,
            base_prefix=base_prefix,
            initial_values={
                f"{base_prefix}/keep": "1",
                f"{base_prefix}/drop": "2",
            },
        )
        subscriber = _with_subscriber(runtime, config)

        if not _wait_for_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/keep",
            payload_text="keep-ready",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "startup baseline keep was not subscribed before remove reload"

        _filestore_post_json(
            host="127.0.0.1",
            port=runtime.filestore_port,
            key_path=runtime.values_key_path,
            payload={f"{base_prefix}/keep": "9"},
            timeout=max(2.0, config.timeout_seconds),
        )
        if not _wait_for_topic_payload(
            client=subscriber,
            topic=f"{base_prefix}/keep",
            payload_text="9",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "remove reload snapshot for keep was not observed"

        if not _wait_for_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/keep",
            payload_text="keep-active",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "remaining key keep not active after remove reload"

        _assert_set_ignored(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/drop",
            payload_text="drop-ignored",
            timeout_seconds=1.0,
        )
        return True, "remove reload removed stale key subscription"
    except Exception as error:
        return False, f"valueservice_reload_remove failed: {error}"
    finally:
        if subscriber is not None:
            subscriber.disconnect()
        _cleanup_runtime(runtime)


def run_valueservice_reload_same_keys_keeps_subscriptions(config) -> tuple[bool, str]:
    runtime = None
    subscriber = None
    try:
        _ensure_binaries()
        base_prefix = _make_topic_root("same_keys")
        runtime = _make_runtime(
            config=config,
            base_prefix=base_prefix,
            initial_values={f"{base_prefix}/a": "old-a", f"{base_prefix}/b": "old-b"},
        )
        subscriber = _with_subscriber(runtime, config)

        _filestore_post_json(
            host="127.0.0.1",
            port=runtime.filestore_port,
            key_path=runtime.values_key_path,
            payload={f"{base_prefix}/a": "new-a", f"{base_prefix}/b": "new-b"},
            timeout=max(2.0, config.timeout_seconds),
        )

        if not _wait_for_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/a",
            payload_text="round-a",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "key a not active after value-only reload"

        _assert_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/b",
            payload_text="round-b",
            timeout_seconds=2.0,
        )
        _assert_set_ignored(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/c",
            payload_text="none",
            timeout_seconds=1.0,
        )
        return True, "value-only reload preserved exact subscription set"
    except Exception as error:
        return False, f"valueservice_reload_same_keys failed: {error}"
    finally:
        if subscriber is not None:
            subscriber.disconnect()
        _cleanup_runtime(runtime)


def run_valueservice_reload_empty_map_unsubscribes_all(config) -> tuple[bool, str]:
    runtime = None
    subscriber = None
    try:
        _ensure_binaries()
        base_prefix = _make_topic_root("empty")
        runtime = _make_runtime(
            config=config,
            base_prefix=base_prefix,
            initial_values={f"{base_prefix}/a": "1", f"{base_prefix}/b": "2"},
        )
        subscriber = _with_subscriber(runtime, config)

        _filestore_post_json(
            host="127.0.0.1",
            port=runtime.filestore_port,
            key_path=runtime.values_key_path,
            payload={},
            timeout=max(2.0, config.timeout_seconds),
        )

        time.sleep(0.8)

        _assert_set_ignored(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/a",
            payload_text="none-a",
            timeout_seconds=1.0,
        )
        _assert_set_ignored(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/b",
            payload_text="none-b",
            timeout_seconds=1.0,
        )
        return True, "empty reload removed all key subscriptions"
    except Exception as error:
        return False, f"valueservice_reload_empty_map failed: {error}"
    finally:
        if subscriber is not None:
            subscriber.disconnect()
        _cleanup_runtime(runtime)


def run_valueservice_new_key_after_reload_accepts_set_immediately(config) -> tuple[bool, str]:
    runtime = None
    subscriber = None
    try:
        _ensure_binaries()
        base_prefix = _make_topic_root("new_after_reload")
        runtime = _make_runtime(
            config=config,
            base_prefix=base_prefix,
            initial_values={f"{base_prefix}/base": "1"},
        )
        subscriber = _with_subscriber(runtime, config)

        if not _wait_for_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/base",
            payload_text="base-ready",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "startup baseline base was not subscribed before reload"

        _filestore_post_json(
            host="127.0.0.1",
            port=runtime.filestore_port,
            key_path=runtime.values_key_path,
            payload={f"{base_prefix}/base": "1", f"{base_prefix}/added": "2"},
            timeout=max(2.0, config.timeout_seconds),
        )
        if not _wait_for_topic_payload(
            client=subscriber,
            topic=f"{base_prefix}/added",
            payload_text="2",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "reload snapshot for added key was not observed"

        if not _wait_for_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/added",
            payload_text="immediate",
            timeout_seconds=max(5.0, config.timeout_seconds),
        ):
            return False, "added key was not immediately active for /set"

        return True, "new key accepts /set immediately after reload"
    except Exception as error:
        return False, f"valueservice_new_key_after_reload failed: {error}"
    finally:
        if subscriber is not None:
            subscriber.disconnect()
        _cleanup_runtime(runtime)


def run_valueservice_unrelated_monitor_event_does_not_change_subscriptions(config) -> tuple[bool, str]:
    runtime = None
    subscriber = None
    try:
        _ensure_binaries()
        base_prefix = _make_topic_root("unrelated")
        runtime = _make_runtime(
            config=config,
            base_prefix=base_prefix,
            initial_values={f"{base_prefix}/base": "1"},
        )
        subscriber = _with_subscriber(runtime, config)

        # Different FileStore key path emits monitoring event but must be ignored by ValueService reload logic.
        _filestore_post_json(
            host="127.0.0.1",
            port=runtime.filestore_port,
            key_path=f"{runtime.values_key_path}-other",
            payload={f"{base_prefix}/new": "2"},
            timeout=max(2.0, config.timeout_seconds),
        )

        if not _wait_for_set_roundtrip(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/base",
            payload_text="base-active",
            timeout_seconds=max(4.0, config.timeout_seconds),
        ):
            return False, "base key unexpectedly inactive after unrelated monitor event"

        _assert_set_ignored(
            client=subscriber,
            host=runtime.broker_host,
            port=runtime.mqtt_port,
            key_topic=f"{base_prefix}/new",
            payload_text="new-ignored",
            timeout_seconds=1.0,
        )
        return True, "unrelated monitor event does not change subscription set"
    except Exception as error:
        return False, f"valueservice_unrelated_monitor_event failed: {error}"
    finally:
        if subscriber is not None:
            subscriber.disconnect()
        _cleanup_runtime(runtime)


TEST_CASES = [
    {
        "name": "yaha/value_service/startup_subscriptions_match_initial_keys",
        "description": "Startup load subscribes exactly configured keys.",
        "run": run_valueservice_startup_subscriptions_match_initial_keys,
    },
    {
        "name": "yaha/value_service/reload_replace_keys_updates_subscriptions",
        "description": "Reload with full key replacement removes old subscriptions and adds new ones.",
        "run": run_valueservice_reload_replace_keys_updates_subscriptions,
    },
    {
        "name": "yaha/value_service/reload_add_key_adds_subscription",
        "description": "Reload key addition adds missing subscription while preserving existing ones.",
        "run": run_valueservice_reload_add_key_adds_subscription,
    },
    {
        "name": "yaha/value_service/reload_remove_key_removes_subscription",
        "description": "Reload key removal unsubscribes removed keys.",
        "run": run_valueservice_reload_remove_key_removes_subscription,
    },
    {
        "name": "yaha/value_service/reload_same_keys_keeps_subscriptions",
        "description": "Value-only reload keeps exact subscription set unchanged.",
        "run": run_valueservice_reload_same_keys_keeps_subscriptions,
    },
    {
        "name": "yaha/value_service/reload_empty_map_unsubscribes_all",
        "description": "Reload with empty map removes all key subscriptions.",
        "run": run_valueservice_reload_empty_map_unsubscribes_all,
    },
    {
        "name": "yaha/value_service/new_key_after_reload_accepts_set_immediately",
        "description": "Newly added key accepts /set immediately after reload.",
        "run": run_valueservice_new_key_after_reload_accepts_set_immediately,
    },
    {
        "name": "yaha/value_service/unrelated_monitor_event_does_not_change_subscriptions",
        "description": "Monitor events for other FileStore key paths must not alter subscriptions.",
        "run": run_valueservice_unrelated_monitor_event_does_not_change_subscriptions,
    },
]
