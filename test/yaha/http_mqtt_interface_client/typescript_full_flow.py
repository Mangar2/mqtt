from __future__ import annotations

import configparser
import importlib.util
import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.request
import uuid
from pathlib import Path


def _load_helper(module_name: str):
    helper_path = Path(__file__).resolve().parents[2] / "integration_tests" / "helpers" / f"{module_name}.py"
    spec = importlib.util.spec_from_file_location(f"integration_helper_{module_name}", helper_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load helper module {module_name} from {helper_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_broker_module = _load_helper("broker")
start_broker = _broker_module.start_broker
stop_broker = _broker_module.stop_broker
resolve_target_host = _broker_module.resolve_target_host

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
_RELEASE_DIR = _PROJECT_ROOT / "build" / "release"
_HTTP_INTERFACE_BINARY = _RELEASE_DIR / ("yahahttpmqttinterfaceclient.exe" if os.name == "nt" else "yahahttpmqttinterfaceclient")
_TS_RUNNER_DIR = Path(__file__).resolve().parent / "ts_runner"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _run_or_raise(command: list[str], label: str, cwd: Path | None = None) -> str:
    completed = subprocess.run(
        command,
        cwd=str(cwd or _PROJECT_ROOT),
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode == 0:
        return completed.stdout

    output_text = "\n".join(
        section.strip() for section in [completed.stdout, completed.stderr] if section and section.strip()
    )
    raise RuntimeError(f"{label} failed with exit code {completed.returncode}: {output_text}")


def _wait_for_http_health(host: str, port: int, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            request = urllib.request.Request(f"http://{host}:{port}/health", method="GET")
            with urllib.request.urlopen(request, timeout=0.4) as response:
                if int(response.getcode()) == 200:
                    return
        except Exception:
            time.sleep(0.05)

    raise TimeoutError(f"HTTP listener did not become healthy on {host}:{port}")


def _write_http_interface_ini(path: Path, *, broker_host: str, broker_port: int, listener_port: int) -> None:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str

    parser["httpMqttInterface"] = {
        "listenerHost": "127.0.0.1",
        "listenerPort": str(listener_port),
        "enablePublishPhpAlias": "true",
        "useLegacyPhpResponse": "false",
    }
    parser["mqtt"] = {
        "host": broker_host,
        "port": str(broker_port),
        "clientId": f"integration-http-interface-{uuid.uuid4().hex[:10]}",
        "reconnectDelayMs": "50",
        "keepAliveMs": "30000",
        "loopSleepMs": "10",
        "enableLifecycleTrace": "false",
        "enableMessageTrace": "false",
        "logReason": "false",
    }

    with path.open("w", encoding="utf-8") as ini_file:
        parser.write(ini_file)


def _compile_typescript_runner(output_dir: Path) -> None:
    _run_or_raise(["tsc", "--version"], "typescript availability check")
    _run_or_raise(
        [
            "tsc",
            "-p",
            str(_TS_RUNNER_DIR / "tsconfig.json"),
            "--noCheck",
            "--outDir",
            str(output_dir),
        ],
        "compile TypeScript integration flow runner",
        cwd=_PROJECT_ROOT,
    )


def _link_or_copy_tree(source_dir: Path, target_dir: Path) -> None:
    if target_dir.exists() or target_dir.is_symlink():
        if target_dir.is_dir() and not target_dir.is_symlink():
            shutil.rmtree(target_dir)
        else:
            target_dir.unlink()

    try:
        target_dir.symlink_to(source_dir, target_is_directory=True)
    except OSError:
        shutil.copytree(source_dir, target_dir)


def _prepare_node_modules(output_dir: Path) -> None:
    scope_root = output_dir / "node_modules" / "@mangar2"
    scope_root.mkdir(parents=True, exist_ok=True)

    compiled_scope_root = output_dir / "spec" / "@mangar2"
    for package_name in ("mqtt-client", "mqtt-utils", "httpservice"):
        package_source = compiled_scope_root / package_name
        if not package_source.exists():
            raise RuntimeError(f"compiled package missing: {package_source}")
        package_target = scope_root / package_name
        _link_or_copy_tree(package_source, package_target)
        package_json = package_target / "package.json"
        package_json.write_text(
            json.dumps(
                {
                    "name": f"@mangar2/{package_name}",
                    "main": "src/index.js",
                },
                separators=(",", ":"),
            ),
            encoding="utf-8",
        )

    compiled_shims = output_dir / "test" / "yaha" / "http_mqtt_interface_client" / "ts_runner" / "shims"
    utils_source = compiled_shims / "mangar2-utils.js"
    checkinput_source = compiled_shims / "checkinput.js"
    if not utils_source.exists() or not checkinput_source.exists():
        raise RuntimeError(f"compiled shim files missing under {compiled_shims}")

    utils_dir = scope_root / "utils"
    utils_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(utils_source, utils_dir / "index.js")

    checkinput_dir = scope_root / "checkinput"
    checkinput_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(checkinput_source, checkinput_dir / "index.js")


def _run_typescript_flow(http_host: str, http_port: int, output_dir: Path) -> dict:
    candidates = sorted(output_dir.rglob("flow_runner.js"))
    if len(candidates) != 1:
        raise RuntimeError(f"compiled flow runner missing or ambiguous under {output_dir}")
    runner_js = candidates[0]

    output = _run_or_raise(
        ["node", str(runner_js), http_host, str(http_port)],
        "run original TypeScript HTTP MQTT flow",
        cwd=output_dir,
    )

    try:
        return json.loads(output.strip() or "{}")
    except json.JSONDecodeError as error:
        raise RuntimeError(f"unable to parse TS flow output as JSON: {error}; output={output!r}")


def run_typescript_full_flow_against_http_mqtt_service(config) -> tuple[bool, str]:
    broker_process = None
    interface_process = None
    working_dir: Path | None = None

    broker_port = _find_free_port()
    http_port = _find_free_port()

    try:
        working_dir = Path(tempfile.mkdtemp(prefix="yaha-http-mqtt-ts-it-"))
        ts_output_dir = working_dir / "tsdist"

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
            cwd=_PROJECT_ROOT,
        )
        if not _HTTP_INTERFACE_BINARY.exists():
            return False, f"http interface binary missing: {_HTTP_INTERFACE_BINARY}"

        ini_path = working_dir / "http_interface.ini"
        _write_http_interface_ini(
            ini_path,
            broker_host=broker_host,
            broker_port=broker_port,
            listener_port=http_port,
        )

        interface_process = subprocess.Popen(
            [str(_HTTP_INTERFACE_BINARY), str(ini_path)],
            cwd=str(_PROJECT_ROOT),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )

        _wait_for_http_health("127.0.0.1", http_port, timeout_seconds=max(2.0, config.timeout_seconds))
        if interface_process.poll() is not None:
            return False, "yahahttpmqttinterfaceclient exited before TypeScript flow started"

        _compile_typescript_runner(ts_output_dir)
        _prepare_node_modules(ts_output_dir)
        flow_result = _run_typescript_flow("127.0.0.1", http_port, ts_output_dir)

        if flow_result.get("ok") is not True:
            return False, f"TypeScript flow result is not ok: {flow_result}"

        received_payloads = set(flow_result.get("receivedPayloads", []))
        expected_payloads = {"payload-qos0", "payload-qos1", "payload-qos2"}
        if not expected_payloads.issubset(received_payloads):
            return False, (
                "TypeScript flow did not receive all expected publish payloads: "
                f"expected={sorted(expected_payloads)} actual={sorted(received_payloads)}"
            )

        return True, (
            "Original TypeScript flow completed successfully against YAHA HTTP MQTT service "
            f"(received={flow_result.get('receivedCount', 0)})"
        )
    except Exception as error:
        return False, f"TypeScript compatibility integration failed: {error}"
    finally:
        if interface_process is not None:
            interface_process.terminate()
            try:
                interface_process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                interface_process.kill()
                interface_process.wait(timeout=3)
        stop_broker(broker_process)
        if working_dir is not None:
            shutil.rmtree(working_dir, ignore_errors=True)


TEST_CASES = [
    {
        "name": "yaha/http_mqtt_interface_client/typescript_full_flow",
        "description": "Original TypeScript client flow (connect/subscribe/publish/ping/unsubscribe/disconnect) against broker-backed HTTP MQTT service",
        "run": run_typescript_full_flow_against_http_mqtt_service,
    }
]
