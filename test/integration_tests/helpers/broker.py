"""Broker process lifecycle helper for integration tests."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
from typing import Any


HELPER_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = HELPER_DIR.parents[2]
RELEASE_DIR = PROJECT_ROOT / "build" / "release"
BROKER_BINARY = RELEASE_DIR / ("mqtt-broker.exe" if os.name == "nt" else "mqtt-broker")

_DEFAULT_WAIT_TIMEOUT_SECONDS = 8.0
_DEFAULT_HOST = "127.0.0.1"
_DEFAULT_MQTT_PORT = 1883
_DEFAULT_WS_PORT = 8083
_TRACE_LEVEL_ENV = "MQTT_INTEGRATION_TRACE_LEVEL"
_TRACE_MODULES_ENV = "MQTT_INTEGRATION_TRACE_MODULES"
_BROKER_OUTPUT_ENV = "MQTT_INTEGRATION_BROKER_OUTPUT"
_TARGET_HOST_ENV = "MQTT_INTEGRATION_TARGET_HOST"
_TARGET_PORT_ENV = "MQTT_INTEGRATION_TARGET_PORT"
_TARGET_WS_PORT_ENV = "MQTT_INTEGRATION_TARGET_WS_PORT"
_BROKER_MANAGED_ENV = "MQTT_INTEGRATION_BROKER_MANAGED"
_EXTERNAL_AUTH_CREDENTIALS_ENV = "MQTT_INTEGRATION_EXTERNAL_AUTH_CREDENTIALS"
_EXTERNAL_SYS_TOPIC_INTERVAL_ENV = "MQTT_INTEGRATION_EXTERNAL_SYS_TOPIC_INTERVAL"
_DEFAULT_BROKER_INI = PROJECT_ROOT / "broker.ini"

_EXTERNAL_SAFE_OVERRIDE_KEYS = {
    "network.mqtt_port",
    "network.ws_port",
    "broker.allow_anonymous",
    "tracing.global_level",
    "tracing.trace_modules",
}

def _external_profile_accepted_overrides() -> dict[str, set[str]]:
    accepted: dict[str, set[str]] = {}

    default_credentials, default_sys_topic_interval = _read_default_profile_from_ini()
    if default_credentials:
        accepted["auth.credential"] = set(default_credentials)
    if default_sys_topic_interval:
        accepted["monitoring.sys_topic_interval"] = {default_sys_topic_interval}

    credentials_raw = os.environ.get(_EXTERNAL_AUTH_CREDENTIALS_ENV, "").strip()
    if credentials_raw:
        credentials = {
            token.strip()
            for token in credentials_raw.split(",")
            if token.strip()
        }
        if credentials:
            accepted.setdefault("auth.credential", set()).update(credentials)

    sys_topic_interval_raw = os.environ.get(_EXTERNAL_SYS_TOPIC_INTERVAL_ENV, "").strip()
    if sys_topic_interval_raw:
        accepted.setdefault("monitoring.sys_topic_interval", set()).add(sys_topic_interval_raw)

    return accepted


def _read_default_profile_from_ini() -> tuple[set[str], str | None]:
    credentials: set[str] = set()
    sys_topic_interval: str | None = None

    try:
        content = _DEFAULT_BROKER_INI.read_text(encoding="utf-8")
    except OSError:
        return credentials, sys_topic_interval

    current_section = ""
    for raw_line in content.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or line.startswith(";"):
            continue

        if line.startswith("[") and line.endswith("]"):
            current_section = line[1:-1].strip().lower()
            continue

        if "=" not in line:
            continue

        key, value = line.split("=", 1)
        key = key.strip().lower()
        value = value.strip()

        if current_section == "auth" and key == "credential" and value:
            credentials.add(value)
        elif current_section == "monitoring" and key == "sys_topic_interval" and value:
            sys_topic_interval = value

    return credentials, sys_topic_interval


class ManagedBrokerRequired(BaseException):
    """Signal to integration runner that this test requires managed broker mode."""


def _is_local_host(hostname: str) -> bool:
    normalized = hostname.strip().lower()
    return normalized in {"localhost", "127.0.0.1", "::1"} or normalized.startswith("127.")


def _read_external_target_host() -> str | None:
    candidate = os.environ.get(_TARGET_HOST_ENV, "").strip()
    return candidate or None


def resolve_target_host(default_host: str = _DEFAULT_HOST) -> str:
    external_host = _read_external_target_host()
    if external_host is None:
        return default_host
    return external_host


def resolve_target_port(default_port: int = _DEFAULT_MQTT_PORT) -> int:
    raw_value = os.environ.get(_TARGET_PORT_ENV, "").strip()
    if not raw_value:
        return default_port
    return int(raw_value)


def resolve_target_ws_port(default_port: int = _DEFAULT_WS_PORT) -> int:
    raw_value = os.environ.get(_TARGET_WS_PORT_ENV, "").strip()
    if raw_value:
        return int(raw_value)
    return resolve_target_port(default_port)


def is_reachable(host: str, port: int, timeout: float) -> bool:
    """Return true if a TCP connect to host:port succeeds within timeout."""
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def start_broker(config_overrides: dict[str, Any] | None = None) -> subprocess.Popen[str] | None:
    """Build and start broker process, then wait until listener is reachable."""
    managed_mode_value = os.environ.get(_BROKER_MANAGED_ENV, "").strip()
    normalized_overrides = _normalize_overrides(config_overrides)

    if managed_mode_value == "0":
        accepted_profile_overrides = _external_profile_accepted_overrides()

        def _matches_external_profile(override_key: str, override_value: Any) -> bool:
            accepted_values = accepted_profile_overrides.get(override_key)
            if accepted_values is None:
                return False

            if isinstance(override_value, bool):
                value_text = "true" if override_value else "false"
            else:
                value_text = str(override_value).strip()

            return value_text in accepted_values

        disallowed_override_keys = sorted(
            override_key
            for override_key in normalized_overrides.keys()
            if not override_key.startswith("__")
            and override_key not in _EXTERNAL_SAFE_OVERRIDE_KEYS
            and not _matches_external_profile(override_key, normalized_overrides[override_key])
        )

        if disallowed_override_keys:
            override_text = ", ".join(disallowed_override_keys)
            raise ManagedBrokerRequired(
                "requires managed broker startup"
                + (f" (requested overrides: {override_text})" if override_text else "")
            )

        if config_overrides is not None:
            config_overrides["network.mqtt_port"] = resolve_target_port(_DEFAULT_MQTT_PORT)
            if "network.ws_port" in config_overrides:
                config_overrides["network.ws_port"] = resolve_target_ws_port(_DEFAULT_WS_PORT)
        return None

    external_target_host = _read_external_target_host()
    if external_target_host is not None and not _is_local_host(external_target_host):
        if config_overrides is not None:
            config_overrides["network.mqtt_port"] = resolve_target_port(_DEFAULT_MQTT_PORT)
            if "network.ws_port" in config_overrides:
                config_overrides["network.ws_port"] = resolve_target_ws_port(_DEFAULT_WS_PORT)
        return None

    _apply_trace_environment_overrides(normalized_overrides)
    host = str(normalized_overrides.get("__host", _DEFAULT_HOST))
    startup_timeout = float(normalized_overrides.get("__startup_timeout_seconds", _DEFAULT_WAIT_TIMEOUT_SECONDS))

    mqtt_port = _extract_effective_port(normalized_overrides)
    config_path = _write_config_if_needed(normalized_overrides)

    _run_or_raise(["cmake", "--preset", "release"], "cmake configure (release)")
    _run_or_raise(
        ["cmake", "--build", "--preset", "release", "--target", "mqtt-broker"],
        "cmake build (mqtt-broker)",
    )

    if not BROKER_BINARY.exists():
        raise RuntimeError(f"broker binary not found at {BROKER_BINARY}")

    command = [str(BROKER_BINARY)]
    if config_path is not None:
        command.append(str(config_path))

    output_mode = os.environ.get(_BROKER_OUTPUT_ENV, "discard").strip().lower()
    inherit_output = output_mode == "inherit"
    stdout_target = None if inherit_output else subprocess.DEVNULL
    stderr_target = None if inherit_output else subprocess.DEVNULL

    process = subprocess.Popen(
        command,
        cwd=PROJECT_ROOT,
        stdout=stdout_target,
        stderr=stderr_target,
        text=True,
    )

    setattr(process, "_integration_config_path", str(config_path) if config_path is not None else "")
    wait_deadline = time.monotonic() + max(0.2, startup_timeout)

    while time.monotonic() < wait_deadline:
        if process.poll() is not None:
            _cleanup_process_config(process)
            raise RuntimeError("broker exited before becoming reachable")
        if is_reachable(host, mqtt_port, timeout=0.3):
            return process
        time.sleep(0.1)

    stop_broker(process)
    raise RuntimeError(f"broker did not become reachable on {host}:{mqtt_port} within {startup_timeout:.1f}s")


def stop_broker(process: subprocess.Popen[str] | None) -> None:
    """Stop broker process using SIGTERM and fallback SIGKILL if needed."""
    if process is None:
        return
    if process.poll() is not None:
        _cleanup_process_config(process)
        return

    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)
    finally:
        _cleanup_process_config(process)


def restart_broker(
    process: subprocess.Popen[str] | None,
    config_overrides: dict[str, Any] | None = None,
) -> subprocess.Popen[str] | None:
    """Restart broker process with optional new configuration overrides."""
    stop_broker(process)
    return start_broker(config_overrides)


def _run_or_raise(command: list[str], label: str) -> None:
    completed = subprocess.run(
        command,
        cwd=PROJECT_ROOT,
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


def _normalize_overrides(config_overrides: dict[str, Any] | None) -> dict[str, Any]:
    if config_overrides is None:
        return {}

    normalized: dict[str, Any] = {}
    for key, value in config_overrides.items():
        if isinstance(value, dict):
            for nested_key, nested_value in value.items():
                normalized[f"{key}.{nested_key}"] = nested_value
        else:
            normalized[key] = value
    return normalized


def _apply_trace_environment_overrides(normalized_overrides: dict[str, Any]) -> None:
    trace_level = os.environ.get(_TRACE_LEVEL_ENV, "").strip().lower()
    if trace_level and "tracing.global_level" not in normalized_overrides:
        normalized_overrides["tracing.global_level"] = trace_level

    trace_modules = os.environ.get(_TRACE_MODULES_ENV, "").strip()
    if trace_modules and "tracing.trace_modules" not in normalized_overrides:
        normalized_overrides["tracing.trace_modules"] = trace_modules


def _extract_effective_port(normalized_overrides: dict[str, Any]) -> int:
    mqtt_override = normalized_overrides.get("network.mqtt_port")
    if mqtt_override is None:
        return _DEFAULT_MQTT_PORT

    effective = int(mqtt_override)
    if effective == 0:
        raise ValueError("network.mqtt_port must be non-zero for reachability checks")
    return effective


def _write_config_if_needed(normalized_overrides: dict[str, Any]) -> Path | None:
    ini_lines = _render_ini_lines(normalized_overrides)
    if not ini_lines:
        return None

    config_dir = Path(tempfile.mkdtemp(prefix="mqtt-integration-config-"))
    config_path = config_dir / "broker.ini"
    config_path.write_text("\n".join(ini_lines) + "\n", encoding="utf-8")
    return config_path


def _render_ini_lines(normalized_overrides: dict[str, Any]) -> list[str]:
    sections: dict[str, dict[str, Any]] = {}
    for full_key, raw_value in normalized_overrides.items():
        if full_key.startswith("__"):
            continue
        if "." not in full_key:
            raise ValueError(
                f"invalid override key {full_key!r}; expected section.key format or nested dict"
            )
        section_name, key_name = full_key.split(".", 1)
        section_map = sections.setdefault(section_name, {})
        section_map[key_name] = raw_value

    lines: list[str] = []
    for section_name in sorted(sections.keys()):
        lines.append(f"[{section_name}]")
        section_map = sections[section_name]
        for key_name in sorted(section_map.keys()):
            lines.append(f"{key_name} = {_format_ini_value(section_map[key_name])}")
        lines.append("")

    if lines and lines[-1] == "":
        lines.pop()
    return lines


def _format_ini_value(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def _cleanup_process_config(process: subprocess.Popen[str]) -> None:
    config_path_text = getattr(process, "_integration_config_path", "")
    if not config_path_text:
        return

    config_path = Path(config_path_text)
    config_dir = config_path.parent
    if config_dir.exists():
        shutil.rmtree(config_dir, ignore_errors=True)
    setattr(process, "_integration_config_path", "")
