"""Integration replay test for msgstore light-on-time timestamp behavior."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
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
MqttClient = _mqtt_client_module.MqttClient
_BROKER_MANAGED_ENV = "MQTT_INTEGRATION_BROKER_MANAGED"
_PROJECT_ROOT = Path(__file__).resolve().parents[3]
_RELEASE_DIR = _PROJECT_ROOT / "build" / "release"
_MSGSTORE_BINARY = _RELEASE_DIR / ("yahamsgstoreclient.exe" if os.name == "nt" else "yahamsgstoreclient")
_BUG_DATA_LOG = _PROJECT_ROOT / "spec" / "bug" / "msgstore-many-entries-from-single-message" / "yahamsgstoreclient_light_on_time_latest.txt"
_BUG_HTTP_RESULT_LOG = _PROJECT_ROOT / "spec" / "bug" / "msgstore-many-entries-from-single-message" / "http_result.log"
_TOPIC = "first/study/main/light/light on time"

_LINE_PATTERN = re.compile(
    r"^[A-Z][a-z]{2}\s+\d+\s+(?P<time>\d{2}:\d{2}:\d{2}).*"
    r"broker: recv topic=(?P<topic>.+?) "
    r"qos=(?P<qos>[0-2]) retain=(?P<retain>[01]) "
    r"value=(?P<value>.*?) reason=count=(?P<reason_count>\d+) latest=\"(?P<latest>.*)\"$"
)


def _require_managed_broker_in_remote(required_overrides: str) -> None:
    if os.environ.get(_BROKER_MANAGED_ENV, "").strip() != "0":
        return
    raise _broker_module.ManagedBrokerRequired(
        f"requires managed broker startup (requested overrides: {required_overrides})"
    )


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
) -> None:
    config_text = "\n".join([
        "[mqtt]",
        f"host = {mqtt_host}",
        f"port = {mqtt_port}",
        f"clientId = msgstore-replay-{uuid.uuid4().hex[:10]}",
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
        "[subscription]",
        "topic = first/study/main/light/#",
        "qos = 1",
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


def _fetch_store_nodes(host: str, port: int, topic: str, timeout_seconds: float) -> list[dict[str, object]]:
    encoded_topic = urllib.parse.quote(topic, safe="/")
    url = f"http://{host}:{port}/store/{encoded_topic}"
    request = urllib.request.Request(url, method="GET")
    request.add_header("levelamount", "5")
    request.add_header("history", "true")
    request.add_header("reason", "true")
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
            _ = _fetch_store_nodes(host, port, _TOPIC, timeout_seconds=min(1.0, timeout_seconds))
            return True
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError):
            time.sleep(0.2)
    return False


def _parse_replay_lines() -> list[dict[str, object]]:
    if not _BUG_DATA_LOG.exists():
        raise RuntimeError(f"missing replay input log: {_BUG_DATA_LOG}")

    events: list[dict[str, object]] = []
    for line in _BUG_DATA_LOG.read_text(encoding="utf-8").splitlines():
        match = _LINE_PATTERN.match(line.strip())
        if match is None:
            continue
        events.append({
            "clock": str(match.group("time")),
            "topic": str(match.group("topic")),
            "qos": int(match.group("qos")),
            "retain": bool(int(match.group("retain"))),
            "value": str(match.group("value")),
            "reason_count": int(match.group("reason_count")),
            "latest": str(match.group("latest")),
        })

    if not events:
        raise RuntimeError("no parsable replay lines found in yahamsgstoreclient_light_on_time_latest.txt")

    return events


def _load_reason_templates_from_http_result() -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    if not _BUG_HTTP_RESULT_LOG.exists():
        raise RuntimeError(f"missing http replay reference: {_BUG_HTTP_RESULT_LOG}")

    raw_text = _BUG_HTTP_RESULT_LOG.read_text(encoding="utf-8")

    try:
        parsed = json.loads(raw_text)
    except json.JSONDecodeError:
        parsed = None

    if parsed is None:
        topic_token = f'"topic": "{_TOPIC}"'
        topic_pos = raw_text.find(topic_token)
        if topic_pos < 0:
            raise RuntimeError("http_result.log partial content does not contain target topic")

        tail = raw_text[topic_pos:]
        reason_pos = tail.find('"reason": [')
        if reason_pos < 0:
            raise RuntimeError("http_result.log partial content does not contain target reason array")

        reason_tail = tail[reason_pos:]
        history_pos = reason_tail.find('"history"')
        if history_pos >= 0:
            reason_tail = reason_tail[:history_pos]

        pair_pattern = re.compile(
            r'"message"\s*:\s*"(?P<message>[^"]+)"\s*,\s*"timestamp"\s*:\s*"(?P<timestamp>[^"]+)"'
        )
        normalized_reason = [
            {
                "message": match.group("message").strip(),
                "timestamp": match.group("timestamp").strip(),
            }
            for match in pair_pattern.finditer(reason_tail)
            if match.group("message").strip() and match.group("timestamp").strip()
        ]

        if len(normalized_reason) < 2:
            raise RuntimeError("http_result.log partial content does not provide usable reason timestamps")

        return normalized_reason, normalized_reason[:2]

    if not isinstance(parsed, list):
        raise RuntimeError("http_result.log root must be a JSON array")

    target_node = None
    for node in parsed:
        if isinstance(node, dict) and str(node.get("topic", "")) == _TOPIC:
            target_node = node
            break

    if target_node is None:
        raise RuntimeError("http_result.log does not contain target topic node")

    reason = target_node.get("reason")
    if not isinstance(reason, list) or len(reason) < 2:
        raise RuntimeError("http_result.log target node reason array is missing or too short")

    normalized_reason: list[dict[str, str]] = []
    for entry in reason:
        if not isinstance(entry, dict):
            continue
        message = str(entry.get("message", "")).strip()
        timestamp = str(entry.get("timestamp", "")).strip()
        if not message or not timestamp:
            continue
        normalized_reason.append({"message": message, "timestamp": timestamp})

    if len(normalized_reason) < 2:
        raise RuntimeError("http_result.log target node does not provide usable reason timestamps")

    return normalized_reason, normalized_reason[:2]


def _build_reason_entries(
    event: dict[str, object],
    main_topic_reason_template: list[dict[str, str]],
    set_topic_reason_template: list[dict[str, str]],
) -> list[dict[str, str]]:
    reason_count = int(event["reason_count"])
    if reason_count <= 0:
        return []

    source_template = main_topic_reason_template
    if str(event["topic"]).endswith("/set"):
        source_template = set_topic_reason_template

    if reason_count <= len(source_template):
        return [dict(entry) for entry in source_template[:reason_count]]

    padded = [dict(entry) for entry in source_template]
    while len(padded) < reason_count:
        padded.append(dict(source_template[-1]))
    return padded


def _build_payload(
    event: dict[str, object],
    main_topic_reason_template: list[dict[str, str]],
    set_topic_reason_template: list[dict[str, str]],
) -> str:
    payload = {
        "message": {
            "topic": str(event["topic"]),
            "value": str(event["value"]),
            "reason": _build_reason_entries(
                event,
                main_topic_reason_template,
                set_topic_reason_template,
            ),
        }
    }
    return json.dumps(payload, separators=(",", ":"))


def _find_topic_node(nodes: list[dict[str, object]], topic: str) -> dict[str, object] | None:
    for node in nodes:
        if str(node.get("topic", "")) == topic:
            return node
    return None


def run_19_4_6_msgstore_replay_light_on_time_detects_stale_timestamp_projection(config) -> tuple[bool, str]:
    broker_process = None
    msgstore_process = None
    working_dir = Path(tempfile.mkdtemp(prefix="mqtt-integration-19-4-6-"))
    mqtt_port = _find_free_port()
    http_port = _find_free_port()
    msgstore_config_path = working_dir / "msgstore.ini"

    try:
        _require_managed_broker_in_remote(
            "isolated mqtt/http ports and local msgstore process for log replay"
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
        )
        msgstore_process = _start_msgstore_process(msgstore_config_path, working_dir)
        if not _wait_for_http_ready("127.0.0.1", http_port, timeout_seconds=max(4.0, config.timeout_seconds)):
            return False, "19.4.6 msgstore HTTP endpoint did not become ready"

        replay_events = _parse_replay_lines()
        main_topic_reason_template, set_topic_reason_template = _load_reason_templates_from_http_result()
        expected_primary_topic_events = [
            event for event in replay_events if str(event["topic"]) == _TOPIC
        ]
        if len(expected_primary_topic_events) < 2:
            return False, "19.4.6 replay input does not contain enough primary-topic events"

        with MqttClient(timeout_seconds=max(2.0, config.timeout_seconds)) as publisher:
            connack = publisher.connect(
                host,
                mqtt_port,
                client_id=f"msgstore-replay-pub-{uuid.uuid4().hex[:10]}",
                clean_start=True,
            )
            assert_connack(connack, reason_code=0x00, session_present=False)

            for event in replay_events:
                payload = _build_payload(
                    event,
                    main_topic_reason_template,
                    set_topic_reason_template,
                )
                assert_reason_code(
                    publisher.publish(
                        str(event["topic"]),
                        payload.encode("utf-8"),
                        qos=int(event["qos"]),
                        retain=bool(event["retain"]),
                    ),
                    0x00,
                )
                time.sleep(0.02)

        time.sleep(0.8)
        nodes = _fetch_store_nodes("127.0.0.1", http_port, _TOPIC, timeout_seconds=max(2.0, config.timeout_seconds))
        topic_node = _find_topic_node(nodes, _TOPIC)
        if topic_node is None:
            return False, "19.4.6 GET result does not contain target topic"

        history = topic_node.get("history")
        if not isinstance(history, list):
            return False, "19.4.6 GET result history is missing or invalid"

        expected_count = len(expected_primary_topic_events)
        if len(history) < expected_count - 1:
            return False, (
                f"19.4.6 insufficient history: expected at least {expected_count - 1}, "
                f"got {len(history)}"
            )

        projection_times = [str(topic_node.get("time", ""))]
        projection_times.extend(str(entry.get("time", "")) for entry in history[: expected_count - 1])
        unique_time_count = len(set(projection_times))

        if unique_time_count < expected_count:
            sample = ", ".join(projection_times[: min(6, len(projection_times))])
            return False, (
                "19.4.6 replay shows stale timestamp projection: "
                f"input_messages={expected_count}, unique_projected_times={unique_time_count}, sample=[{sample}]"
            )

        return True, (
            "19.4.6 replay kept distinct projected timestamps for replayed messages: "
            f"input_messages={expected_count}, unique_projected_times={unique_time_count}"
        )
    except Exception as error:
        return False, f"19.4.6 failed: {error}"
    finally:
        stop_broker(broker_process)
        _stop_process(msgstore_process)
        shutil.rmtree(working_dir, ignore_errors=True)


TEST_CASES = [
    {
        "name": "robustness/msgstore_replay_light_on_time_detects_stale_timestamp_projection",
        "description": "Replay light_on_time msgstore recv log lines and verify GET projection for stale timestamp collapse",
        "run": run_19_4_6_msgstore_replay_light_on_time_detects_stale_timestamp_projection,
    }
]
