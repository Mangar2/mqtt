#!/usr/bin/env python3
"""Repro for bug: second HTTP publish on same /set topic is not logged as inbound by ValueService."""

from __future__ import annotations

import configparser
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
from datetime import datetime
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[3]
RELEASE_DIR = PROJECT_ROOT / "build" / "release"
FILESTORE_BINARY = RELEASE_DIR / ("yahafilestoreclient.exe" if os.name == "nt" else "yahafilestoreclient")
VALUE_SERVICE_BINARY = RELEASE_DIR / ("yahavalueserviceclient.exe" if os.name == "nt" else "yahavalueserviceclient")
HTTP_INTERFACE_BINARY = RELEASE_DIR / ("yahahttpmqttinterfaceclient.exe" if os.name == "nt" else "yahahttpmqttinterfaceclient")
BROKER_BINARY = RELEASE_DIR / ("yahabroker.exe" if os.name == "nt" else "yahabroker")
BUG_ROOT = Path(__file__).resolve().parent
LOG_ROOT = BUG_ROOT / "logs"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _wait_for_listener(host: str, port: int, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise TimeoutError(f"listener did not become reachable on {host}:{port}")


def _wait_for_filestore_ready(host: str, port: int, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            request = urllib.request.Request(f"http://{host}:{port}/healthz", method="GET")
            with urllib.request.urlopen(request, timeout=0.6):
                return
        except urllib.error.HTTPError as error:
            if int(error.code) in (400, 404):
                return
        except Exception:
            time.sleep(0.1)
    raise TimeoutError(f"filestore endpoint did not become ready on {host}:{port}")


def _write_ini(path: Path, sections: dict[str, dict[str, str]]) -> None:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    for section, values in sections.items():
        parser[section] = values
    with path.open("w", encoding="utf-8") as ini_file:
        parser.write(ini_file)


def _post_json(url: str, payload: dict[str, object], expected_status: int) -> None:
    request = urllib.request.Request(
        url=url,
        data=json.dumps(payload, separators=(",", ":")).encode("utf-8"),
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=2.0) as response:
        status_code = int(response.getcode())
    if status_code != expected_status:
        raise RuntimeError(f"unexpected HTTP status {status_code} for {url}, expected {expected_status}")


def _terminate(process: subprocess.Popen[str] | None) -> None:
    if process is None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def _collect_valueservice_logs(
    process: subprocess.Popen[str],
    timeout_seconds: float,
    sink,
) -> list[str]:
    deadline = time.monotonic() + timeout_seconds
    lines: list[str] = []

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

        text = line.rstrip("\n")
        if text:
            lines.append(text)
            sink.write(text + "\n")
            sink.flush()

    return lines


def _find_first_index(lines: list[str], predicate) -> int:
    for index, line in enumerate(lines):
        if predicate(line):
            return index
    return -1


def _validate_interaction_sequence(lines: list[str], set_topic: str, base_topic: str) -> tuple[bool, str]:
    set_in_25 = _find_first_index(lines, lambda line: f"value_service[in] topic={set_topic}" in line and "value=25" in line)
    out_25 = _find_first_index(lines, lambda line: f"value_service[out] topic={base_topic}" in line and "value=25" in line)
    monitor_http_post = _find_first_index(
        lines,
        lambda line: (
            "value_service[in] topic=$MONITOR/FileStore/" in line
            and "/changed qos=" in line
            and '"source":"http-post"' in line
            and '"keyPath":"/valueservice/values' in line
        ),
    )
    out_reload = _find_first_index(
        lines,
        lambda line: (
            f"value_service[out] topic={base_topic}" in line
            and "reason=\"reloaded after valuestore file change\"" in line
        ),
    )
    monitor_null = _find_first_index(
        lines,
        lambda line: (
            "value_service[in] topic=$MONITOR/FileStore/" in line
            and "/changed qos=" in line
            and '"keyPath":null' in line
            and '"source":"filesystem-watch"' in line
        ),
    )

    if set_in_25 < 0:
        return False, "missing first value_service[in] for /set value=25"
    if out_25 < 0:
        return False, "missing value_service[out] publish for value=25"
    if monitor_http_post < 0:
        return False, "missing monitor changed event from http-post with keyPath=/valueservice/values"
    if out_reload < 0:
        return False, "missing reload publish after monitor changed event"

    if monitor_null >= 0:
        if not (set_in_25 <= out_25 <= monitor_http_post <= out_reload <= monitor_null):
            return False, "interaction events found but in unexpected order"
    else:
        if not (set_in_25 <= out_25 <= monitor_http_post <= out_reload):
            return False, "interaction events found but in unexpected order"

    return True, "interaction sequence reproduced"


def main() -> int:
    if not BROKER_BINARY.exists() or not FILESTORE_BINARY.exists() or not VALUE_SERVICE_BINARY.exists() or not HTTP_INTERFACE_BINARY.exists():
        print("FAIL: required release binaries are missing")
        print(f"  broker={BROKER_BINARY}")
        print(f"  filestore={FILESTORE_BINARY}")
        print(f"  valueservice={VALUE_SERVICE_BINARY}")
        print(f"  http_interface={HTTP_INTERFACE_BINARY}")
        return 2

    broker_process = None
    filestore_process = None
    valueservice_process = None
    http_process = None
    work_dir: Path | None = None
    values_log_file = None
    http_log_file = None
    run_log_dir: Path | None = None

    base_topic = "ground/wardrobe/floorHeating/lowtemperature"
    set_topic = f"{base_topic}/set"

    try:
        LOG_ROOT.mkdir(parents=True, exist_ok=True)
        run_stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        run_log_dir = LOG_ROOT / f"{run_stamp}-{uuid.uuid4().hex[:8]}"
        run_log_dir.mkdir(parents=True, exist_ok=True)
        values_log_path = run_log_dir / "valueservice.log"
        http_log_path = run_log_dir / "httpinterface.log"
        values_log_file = values_log_path.open("w", encoding="utf-8")
        http_log_file = http_log_path.open("w", encoding="utf-8")

        broker_port = _find_free_port()
        filestore_port = _find_free_port()
        http_port = _find_free_port()

        work_dir = Path(tempfile.mkdtemp(prefix="bug-http-publish-not-received-"))
        data_dir = work_dir / "data"
        broker_data_dir = work_dir / "broker-data"
        data_dir.mkdir(parents=True, exist_ok=True)
        broker_data_dir.mkdir(parents=True, exist_ok=True)

        values_key_path = f"/valueservice/values/{uuid.uuid4().hex}"
        monitor_prefix = f"$MONITOR/FileStore/{uuid.uuid4().hex}"

        broker_ini = work_dir / "broker.ini"
        filestore_ini = work_dir / "filestore.ini"
        values_ini = work_dir / "valueservice.ini"
        http_ini = work_dir / "http_interface.ini"

        _write_ini(
            broker_ini,
            {
                "network": {"mqtt_port": str(broker_port), "ws_port": "0"},
                "broker": {"allow_anonymous": "true"},
                "persistence": {"mode": "full", "dir": str(broker_data_dir)},
            },
        )
        _write_ini(
            filestore_ini,
            {
                "mqtt": {
                    "host": "127.0.0.1",
                    "port": str(broker_port),
                    "clientId": f"bug-filestore-{uuid.uuid4().hex[:8]}",
                    "reconnectDelayMs": "50",
                    "keepAliveIntervalMs": "1000",
                    "loopSleepMs": "10",
                },
                "server": {"host": "127.0.0.1", "port": str(filestore_port)},
                "filestore": {"directory": str(data_dir), "keepFiles": "2", "maxKeyLength": "255"},
                "monitoring": {
                    "enabled": "true",
                    "topicPrefix": monitor_prefix,
                    "qos": "1",
                    "retain": "false",
                    "watchIntervalMs": "50",
                },
            },
        )
        _write_ini(
            values_ini,
            {
                "mqtt": {
                    "host": "127.0.0.1",
                    "port": str(broker_port),
                    "clientId": f"bug-values-{uuid.uuid4().hex[:8]}",
                    "reconnectDelayMs": "50",
                    "keepAliveIntervalMs": "1000",
                    "loopSleepMs": "10",
                },
                "filestore": {
                    "use": "true",
                    "host": "127.0.0.1",
                    "port": str(filestore_port),
                    "filename": values_key_path,
                    "topicPrefix": monitor_prefix,
                },
                "valueservice": {"subscribeQoS": "1"},
            },
        )
        _write_ini(
            http_ini,
            {
                "httpMqttInterface": {
                    "listenerHost": "127.0.0.1",
                    "listenerPort": str(http_port),
                    "enablePublishPhpAlias": "true",
                    "useLegacyPhpResponse": "false",
                },
                "mqtt": {
                    "host": "127.0.0.1",
                    "port": str(broker_port),
                    "clientId": f"bug-http-{uuid.uuid4().hex[:8]}",
                    "reconnectDelayMs": "50",
                    "keepAliveIntervalMs": "30000",
                    "loopSleepMs": "10",
                    "logReason": "false",
                },
            },
        )

        broker_process = subprocess.Popen([str(BROKER_BINARY), str(broker_ini)], cwd=work_dir)

        filestore_process = subprocess.Popen(
            [str(FILESTORE_BINARY), str(filestore_ini)],
            cwd=work_dir,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        _wait_for_filestore_ready("127.0.0.1", filestore_port, 3.0)

        _post_json(f"http://127.0.0.1:{filestore_port}{values_key_path}", {base_topic: "0"}, 200)

        valueservice_process = subprocess.Popen(
            [str(VALUE_SERVICE_BINARY), str(values_ini)],
            cwd=work_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        http_process = subprocess.Popen(
            [str(HTTP_INTERFACE_BINARY), str(http_ini)],
            cwd=work_dir,
            stdout=http_log_file,
            stderr=subprocess.STDOUT,
            text=True,
        )
        _wait_for_listener("127.0.0.1", http_port, 3.0)

        time.sleep(0.6)

        _post_json(
            f"http://127.0.0.1:{http_port}/publish",
            {"topic": set_topic, "value": "25", "reason": [{"message": "bug repro", "timestamp": "2026-05-12T20:46:53Z"}]},
            204,
        )

        first_window_logs = _collect_valueservice_logs(valueservice_process, 2.0, values_log_file)
        sequence_ok, sequence_detail = _validate_interaction_sequence(first_window_logs, set_topic, base_topic)

        _post_json(
            f"http://127.0.0.1:{http_port}/publish",
            {"topic": set_topic, "value": "23", "reason": [{"message": "bug repro", "timestamp": "2026-05-12T20:47:54Z"}]},
            204,
        )

        second_window_logs = _collect_valueservice_logs(valueservice_process, 2.0, values_log_file)
        all_logs = first_window_logs + second_window_logs
        in_hits = [
            line
            for line in all_logs
            if f"value_service[in] topic={set_topic}" in line
        ]

        if len(in_hits) >= 2:
            print("FAIL: production symptom not reproduced; second inbound set log is present")
            print(f"  topic={set_topic}")
            print(
                "  interaction="
                + ("FileStore reload sequence reproduced" if sequence_ok else f"sequence mismatch ({sequence_detail})")
            )
            print(f"  valueservice_log={values_log_path}")
            print(f"  httpinterface_log={http_log_path}")
            for line in in_hits:
                print(f"  hit: {line}")
            return 1

        if not sequence_ok:
            print("FAIL: production symptom reproduced but interaction sequence mismatch")
            print(f"  reason={sequence_detail}")
            print(f"  valueservice_log={values_log_path}")
            print(f"  httpinterface_log={http_log_path}")
            return 1

        print("PASS: reproduced production symptom")
        print("  interaction=FileStore reload sequence reproduced")
        print("  second inbound set log is missing")
        print(f"  valueservice_log={values_log_path}")
        print(f"  httpinterface_log={http_log_path}")
        for line in in_hits:
            print(f"  hit: {line}")
        return 0
    finally:
        _terminate(http_process)
        _terminate(valueservice_process)
        _terminate(filestore_process)
        _terminate(broker_process)
        if values_log_file is not None:
            values_log_file.close()
        if http_log_file is not None:
            http_log_file.close()
        if work_dir is not None:
            shutil.rmtree(work_dir, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
