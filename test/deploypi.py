#!/usr/bin/env python3
from __future__ import annotations

"""Build ARMv6 broker binary and deploy it to a Raspberry Pi via scp."""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
ARMV6_PRESET = "armv6-zig-release"
DEFAULT_TARGET = "mqtt-broker"
DEFAULT_REMOTE_HOST = "pi@raspberrypi"
DEFAULT_REMOTE_DIR = "~/mqtt"


def run_or_fail(command: list[str], *, label: str) -> None:
    print(f"[deploy-armv6] {label}: {' '.join(command)}")
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
    args = parser.parse_args()

    try:
        require_tool("cmake")
        require_tool("ssh")
        require_tool("scp")

        run_or_fail(
            ["cmake", "--preset", ARMV6_PRESET, "--fresh"],
            label="configure",
        )
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

        run_or_fail(
            ["ssh", args.remote_host, f"mkdir -p {args.remote_dir}"],
            label="create remote directory",
        )
        run_or_fail(
            [
                "scp",
                str(binary_path),
                f"{args.remote_host}:{args.remote_dir}/mqtt-broker",
            ],
            label="copy binary",
        )

        print("[deploy-armv6] done")
        print(
            f"[deploy-armv6] deployed {binary_path} -> "
            f"{args.remote_host}:{args.remote_dir}/mqtt-broker"
        )
        return 0
    except Exception as error:
        print(f"[deploy-armv6] failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
