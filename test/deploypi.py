#!/usr/bin/env python3
from __future__ import annotations

"""Build ARMv6 broker binary and deploy it to a Raspberry Pi via scp."""

import argparse
import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path
import shlex


PROJECT_ROOT = Path(__file__).resolve().parents[1]
ARMV6_PRESET = "armv6-zig-release"
DEFAULT_TARGET = "mqtt-broker"
DEFAULT_REMOTE_HOST = "pi@raspberrypi"
DEFAULT_REMOTE_DIR = "~/mqtt"
DEFAULT_REMOTE_PORT = 1883
DEFAULT_START_TIMEOUT_SECONDS = 12.0
DEFAULT_REMOTE_CONFIG = "broker.ws.ini"


def run_or_fail(command: list[str], *, label: str) -> None:
    print(f"STEP {label}...")
    command_env = dict(os.environ)
    # Avoid leaking caller toolchain overrides into preset-driven builds.
    command_env.pop("CMAKE_TOOLCHAIN_FILE", None)
    completed = subprocess.run(
        command,
        cwd=PROJECT_ROOT,
        check=False,
        env=command_env,
    )
    if completed.returncode != 0:
        raise RuntimeError(f"{label} failed with exit code {completed.returncode}")
    print(f"STEP {label} ok")


def shell_join(parts: list[str]) -> str:
    return " ".join(shlex.quote(part) for part in parts)


def split_remote_host(remote_host: str) -> str:
    if "@" in remote_host:
        return remote_host.split("@", 1)[1]
    return remote_host


def remote_shell_dir(path: str) -> str:
    stripped = path.strip()
    if stripped.startswith("~"):
        return stripped
    return shlex.quote(stripped)


def wait_for_tcp(host: str, port: int, timeout_seconds: float) -> bool:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.8):
                return True
        except OSError:
            time.sleep(0.3)
    return False


def configure_if_needed(preset: str) -> str:
    cache_path = PROJECT_ROOT / "build" / preset / "CMakeCache.txt"
    if cache_path.exists():
        return "incremental"

    run_or_fail(["cmake", "--preset", preset], label="configure")
    return "fresh"


def stop_remote_broker(remote_host: str) -> None:
    run_or_fail(
        [
            "ssh",
            "-n",
            "-o",
            "BatchMode=yes",
            remote_host,
            "pkill -x mqtt-broker >/dev/null 2>&1 || true",
        ],
        label="stop remote broker",
    )


def start_remote_broker(remote_host: str, remote_dir: str,
                        remote_config: str) -> None:
    start_script = (
        f"cd {remote_shell_dir(remote_dir)} && "
        "if command -v setsid >/dev/null 2>&1; then "
        "setsid -f ./mqtt-broker "
        f"{shlex.quote(remote_config)} "
        "> broker.log 2>&1 < /dev/null; "
        "else "
        "nohup ./mqtt-broker "
        f"{shlex.quote(remote_config)} "
        "> broker.log 2>&1 < /dev/null & "
        "fi; "
        "pgrep -n -x mqtt-broker > broker.pid || true"
    )
    run_or_fail(
        ["ssh", "-n", "-o", "BatchMode=yes", remote_host, start_script],
        label="start remote broker",
    )


def require_tool(name: str) -> None:
    if shutil.which(name) is None:
        raise RuntimeError(f"required tool not found in PATH: {name}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build armv6 broker with Zig and copy binary to Raspberry Pi."
    )
    parser.add_argument("--target", default=DEFAULT_TARGET, help="CMake build target")
    parser.add_argument(
        "--binary",
        default="",
        help="Explicit local binary path (default: build/<preset>/mqtt-broker)",
    )
    parser.add_argument(
        "--remote-host",
        default=DEFAULT_REMOTE_HOST,
        help="Remote SSH host (user@host)",
    )
    parser.add_argument(
        "--remote-dir",
        default=DEFAULT_REMOTE_DIR,
        help="Remote destination directory",
    )
    parser.add_argument(
        "--remote-port",
        type=int,
        default=DEFAULT_REMOTE_PORT,
        help="Remote MQTT port used for startup verification",
    )
    parser.add_argument(
        "--start-timeout",
        type=float,
        default=DEFAULT_START_TIMEOUT_SECONDS,
        help="Seconds to wait for broker port after restart",
    )
    parser.add_argument(
        "--remote-config",
        default=DEFAULT_REMOTE_CONFIG,
        help="Remote broker config file used on start (for example broker.ws.ini)",
    )
    args = parser.parse_args()

    try:
        require_tool("cmake")
        require_tool("ssh")
        require_tool("scp")

        build_mode = configure_if_needed(ARMV6_PRESET)
        run_or_fail(
            ["cmake", "--build", "--preset", ARMV6_PRESET, "--target", args.target],
            label="build",
        )

        binary_path = (
            Path(args.binary).expanduser()
            if args.binary
            else (PROJECT_ROOT / "build" / ARMV6_PRESET / "mqtt-broker")
        )
        if not binary_path.exists():
            raise RuntimeError(f"build finished but binary not found: {binary_path}")

        stop_remote_broker(args.remote_host)

        run_or_fail(
            [
                "ssh",
                "-n",
                "-o",
                "BatchMode=yes",
                args.remote_host,
                f"mkdir -p {remote_shell_dir(args.remote_dir)}",
            ],
            label="create remote directory",
        )

        remote_temp = f"{args.remote_dir}/mqtt-broker.new"
        run_or_fail(
            [
                "scp",
                "-o",
                "BatchMode=yes",
                str(binary_path),
                f"{args.remote_host}:{remote_temp}",
            ],
            label="copy binary",
        )
        run_or_fail(
            [
                "ssh",
                "-n",
                "-o",
                "BatchMode=yes",
                args.remote_host,
                f"cd {remote_shell_dir(args.remote_dir)} && "
                + shell_join(["mv", "mqtt-broker.new", "mqtt-broker"])
                + " && "
                + shell_join(["chmod", "+x", "mqtt-broker"]),
            ],
            label="activate binary",
        )

        start_remote_broker(args.remote_host, args.remote_dir,
                    args.remote_config)

        remote_name = split_remote_host(args.remote_host)
        if not wait_for_tcp(remote_name, args.remote_port, args.start_timeout):
            raise RuntimeError(
                f"remote broker did not become reachable at {remote_name}:{args.remote_port}"
            )

        print(
            "DEPLOY ok"
            f" mode={build_mode}"
            f" host={args.remote_host}"
            f" port={args.remote_port}"
            f" config={args.remote_config}"
            f" target={args.target}"
        )
        return 0
    except Exception as error:
        print(f"DEPLOY fail error={error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
