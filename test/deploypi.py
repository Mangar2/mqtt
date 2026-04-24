#!/usr/bin/env python3
from __future__ import annotations

"""Build ARMv6 broker binary and deploy it to a Raspberry Pi via scp."""

import argparse
import hashlib
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
DEFAULT_LOG_TAIL_LINES = 80
TRACE_LEVEL_CHOICES = ("none", "error", "warning", "info", "trace")


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
                        remote_config: str,
                        trace_level: str,
                        trace_modules: list[str] | None = None) -> None:
    trace_level_argument = ""
    if trace_level:
        trace_level_argument = f" --trace-level={shlex.quote(trace_level)}"

    trace_module_arguments = ""
    if trace_modules:
        trace_module_arguments = " " + " ".join(
            f"--trace-module={shlex.quote(m)}" for m in trace_modules
        )

    start_script = (
        f"cd {remote_shell_dir(remote_dir)} && "
        "if command -v setsid >/dev/null 2>&1; then "
        "setsid -f ./mqtt-broker "
        f"{shlex.quote(remote_config)} "
        f"{trace_level_argument} "
        f"{trace_module_arguments} "
        "> broker.log 2>&1 < /dev/null; "
        "else "
        "nohup ./mqtt-broker "
        f"{shlex.quote(remote_config)} "
        f"{trace_level_argument} "
        f"{trace_module_arguments} "
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


def sha256_file(file_path: Path) -> str:
    digest = hashlib.sha256()
    with file_path.open("rb") as file_handle:
        for chunk in iter(lambda: file_handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_remote_binary_sha256(remote_host: str, remote_dir: str) -> str | None:
    remote_command = (
        f"cd {remote_shell_dir(remote_dir)} && "
        "if [ ! -f mqtt-broker ]; then "
        "echo MISSING; "
        "elif command -v sha256sum >/dev/null 2>&1; then "
        "sha256sum mqtt-broker | awk '{print $1}'; "
        "elif command -v shasum >/dev/null 2>&1; then "
        "shasum -a 256 mqtt-broker | awk '{print $1}'; "
        "else "
        "echo NOHASH; "
        "fi"
    )
    completed = subprocess.run(
        ["ssh", "-n", "-o", "BatchMode=yes", remote_host, remote_command],
        cwd=PROJECT_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        stderr_text = completed.stderr.strip()
        raise RuntimeError(
            "failed to read remote broker checksum"
            + (f": {stderr_text}" if stderr_text else "")
        )

    output_lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    if not output_lines:
        return None

    first_line = output_lines[0]
    if first_line == "MISSING":
        return None
    if first_line == "NOHASH":
        raise RuntimeError("remote host has neither sha256sum nor shasum")
    return first_line


def show_remote_log_tail(remote_host: str, remote_dir: str, lines: int) -> None:
    tail_command = (
        f"cd {remote_shell_dir(remote_dir)} && "
        f"tail -n {lines} broker.log || true"
    )
    run_or_fail(
        ["ssh", "-n", "-o", "BatchMode=yes", remote_host, tail_command],
        label="show remote broker log",
    )


def follow_remote_log(remote_host: str, remote_dir: str, lines: int) -> None:
    follow_command = (
        f"cd {remote_shell_dir(remote_dir)} && "
        f"tail -n {lines} -f broker.log"
    )
    print("STEP follow remote broker log...")
    print("Press Ctrl+C to stop log streaming. Broker keeps running on the Pi.")
    subprocess.run(
        ["ssh", "-o", "BatchMode=yes", remote_host, follow_command],
        cwd=PROJECT_ROOT,
        check=False,
    )
    print("STEP follow remote broker log ok")


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
    parser.add_argument(
        "--trace-level",
        choices=TRACE_LEVEL_CHOICES,
        default="",
        help=(
            "Optional broker CLI trace level override. "
            "When omitted, tracing level from remote config is used."
        ),
    )
    parser.add_argument(
        "--trace-module",
        dest="trace_modules",
        action="append",
        default=[],
        metavar="MODULE",
        help="Repeatable: add a trace module override (e.g. connection, will_manager).",
    )
    parser.add_argument(
        "--show-log",
        action="store_true",
        help="Show the remote broker log tail after startup verification",
    )
    parser.add_argument(
        "--follow-log",
        action="store_true",
        help="Follow remote broker log after deploy (Ctrl+C to stop following)",
    )
    parser.add_argument(
        "--log-lines",
        type=int,
        default=DEFAULT_LOG_TAIL_LINES,
        help="Number of log lines used by --show-log or as initial lines for --follow-log",
    )
    args = parser.parse_args()

    try:
        require_tool("cmake")
        require_tool("ssh")
        require_tool("scp")
        if args.log_lines <= 0:
            raise RuntimeError("--log-lines must be > 0")

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

        local_binary_sha256 = sha256_file(binary_path)
        remote_binary_sha256 = read_remote_binary_sha256(args.remote_host, args.remote_dir)
        should_copy_binary = remote_binary_sha256 != local_binary_sha256

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

        if should_copy_binary:
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
        else:
            print("STEP copy binary skipped (unchanged build output)")
            print("STEP activate binary skipped (unchanged build output)")

        start_remote_broker(args.remote_host, args.remote_dir,
                args.remote_config, args.trace_level,
                args.trace_modules or None)

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
            f" trace_level={args.trace_level or '<config>'}"
        )

        if args.show_log:
            show_remote_log_tail(args.remote_host, args.remote_dir, args.log_lines)

        if args.follow_log:
            follow_remote_log(args.remote_host, args.remote_dir, args.log_lines)

        return 0
    except Exception as error:
        print(f"DEPLOY fail error={error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
