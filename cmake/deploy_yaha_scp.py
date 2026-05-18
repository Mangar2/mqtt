#!/usr/bin/env python3
from __future__ import annotations

"""Copy a generated YAHA deployment directory to a remote host via scp.

Features:
- Creates missing remote directories.
- Compares file hashes before copying and skips identical files.
- Prompts before overwriting changed protected INI config files (.ini).
- Optional remote install/restart for selected service components.
- Optional remote execution of root install.sh.
- Remote install can run with interactive TTY for sudo password prompts.
"""

import argparse
import hashlib
import os
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOCAL_DIR = PROJECT_ROOT / "deployment" / "yaha"
DEFAULT_REMOTE_HOST = "pi@raspberrypi"
DEFAULT_REMOTE_DIR = "~/mqtt/yaha"
KNOWN_INSTALL_COMPONENTS = (
    "broker",
    "filestore",
    "msgstore",
    "automation",
    "valueservice",
    "rs485interface",
    "brokerconnector",
    "httpmqttinterface",
    "remoteservice",
)


@dataclass
class CopyStats:
    copied: int = 0
    skipped_identical: int = 0
    skipped_prompt: int = 0


def run_capture(command: list[str], *, cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=str(cwd),
        check=False,
        capture_output=True,
        text=True,
    )


def run_or_fail(command: list[str], *, cwd: Path, label: str) -> None:
    completed = subprocess.run(command, cwd=str(cwd), check=False)
    if completed.returncode != 0:
        raise RuntimeError(f"{label} failed with exit code {completed.returncode}")


def normalize_remote_host(remote_host: str) -> str:
    if "@" not in remote_host:
        return f"pi@{remote_host}"
    return remote_host


def remote_shell_path(path: str) -> str:
    stripped = path.strip()
    if stripped.startswith("~"):
        return stripped
    return shlex.quote(stripped)


def sha256_file(file_path: Path) -> str:
    digest = hashlib.sha256()
    with file_path.open("rb") as file_handle:
        for chunk in iter(lambda: file_handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def remote_exists_file(*, remote_host: str, remote_path: str, cwd: Path) -> bool:
    check_command = (
        f"if [ -f {remote_shell_path(remote_path)} ]; then echo YES; else echo NO; fi"
    )
    completed = run_capture(
        ["ssh", "-n", "-o", "BatchMode=yes", remote_host, check_command],
        cwd=cwd,
    )
    if completed.returncode != 0:
        stderr_text = completed.stderr.strip()
        raise RuntimeError(
            "failed to check remote file existence"
            + (f": {stderr_text}" if stderr_text else "")
        )
    return completed.stdout.strip() == "YES"


def remote_sha256(*, remote_host: str, remote_path: str, cwd: Path) -> str | None:
    command = (
        f"if [ ! -f {remote_shell_path(remote_path)} ]; then "
        "echo MISSING; "
        "elif command -v sha256sum >/dev/null 2>&1; then "
        f"sha256sum {remote_shell_path(remote_path)} | awk '{{print $1}}'; "
        "elif command -v shasum >/dev/null 2>&1; then "
        f"shasum -a 256 {remote_shell_path(remote_path)} | awk '{{print $1}}'; "
        "else "
        "echo NOHASH; "
        "fi"
    )
    completed = run_capture(
        ["ssh", "-n", "-o", "BatchMode=yes", remote_host, command],
        cwd=cwd,
    )
    if completed.returncode != 0:
        stderr_text = completed.stderr.strip()
        raise RuntimeError(
            "failed to calculate remote checksum"
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


def is_protected_service_config_file(path: Path) -> bool:
    suffix = path.suffix.lower()
    # --no-overwrite-ini must only protect runtime-edited INI files.
    # Service unit templates are deployment-owned and must be updated.
    return suffix == ".ini"


def list_local_files(local_root: Path) -> list[Path]:
    file_list: list[Path] = []
    for path in sorted(local_root.rglob("*")):
        if path.is_file():
            file_list.append(path)
    return file_list


def list_local_dirs(local_root: Path) -> list[Path]:
    dir_list: list[Path] = [local_root]
    for path in sorted(local_root.rglob("*")):
        if path.is_dir():
            dir_list.append(path)
    return dir_list


def prompt_overwrite(relative_path: Path) -> str:
    while True:
        answer = input(
            "Remote protected config exists and differs: "
            f"{relative_path.as_posix()}\n"
            "Overwrite? [y]es / [n]o: "
        ).strip().lower()
        if answer in {"y", "n"}:
            return answer
        print("Please answer with y or n.")


def build_remote_path(remote_root: str, relative_path: Path) -> str:
    cleaned_root = remote_root.rstrip("/")
    if relative_path.as_posix() == ".":
        return cleaned_root
    return f"{cleaned_root}/{relative_path.as_posix()}"


def ensure_remote_directories(
    *,
    remote_host: str,
    remote_root: str,
    local_root: Path,
    cwd: Path,
) -> None:
    for directory in list_local_dirs(local_root):
        relative = directory.relative_to(local_root)
        remote_directory = build_remote_path(remote_root, relative)
        command = f"mkdir -p {remote_shell_path(remote_directory)}"
        run_or_fail(
            ["ssh", "-n", "-o", "BatchMode=yes", remote_host, command],
            cwd=cwd,
            label=f"create remote directory {remote_directory}",
        )


def copy_file(
    *,
    local_file: Path,
    remote_host: str,
    remote_file: str,
    cwd: Path,
) -> None:
    remote_temp = f"{remote_file}.new.{os.getpid()}"
    run_or_fail(
        [
            "scp",
            "-o",
            "BatchMode=yes",
            str(local_file),
            f"{remote_host}:{remote_temp}",
        ],
        cwd=cwd,
        label=f"copy file {local_file.name}",
    )

    executable = bool(local_file.stat().st_mode & 0o111)
    activate_command = (
        f"mv {remote_shell_path(remote_temp)} {remote_shell_path(remote_file)}"
    )
    if executable:
        activate_command += f" && chmod +x {remote_shell_path(remote_file)}"

    run_or_fail(
        ["ssh", "-n", "-o", "BatchMode=yes", remote_host, activate_command],
        cwd=cwd,
        label=f"activate file {local_file.name}",
    )


def run_remote_component_install(
    *,
    remote_host: str,
    remote_root: str,
    component_name: str,
    cwd: Path,
    interactive: bool,
) -> None:
    install_script = build_remote_path(remote_root, Path(component_name) / "install.sh")
    command = (
        f"if [ ! -x {remote_shell_path(install_script)} ]; then "
        f"echo 'missing installer: {install_script}' >&2; "
        "exit 1; "
        "fi; "
        f"bash {remote_shell_path(install_script)}"
    )
    ssh_command = ["ssh", "-tt", remote_host, command] if interactive else [
        "ssh", "-n", "-o", "BatchMode=yes", remote_host, command
    ]
    run_or_fail(ssh_command, cwd=cwd, label=f"remote install {component_name}")


def run_remote_root_install(
    *,
    remote_host: str,
    remote_root: str,
    cwd: Path,
    interactive: bool,
) -> None:
    install_script = build_remote_path(remote_root, Path("install.sh"))
    command = (
        f"if [ ! -x {remote_shell_path(install_script)} ]; then "
        f"echo 'missing root installer: {install_script}' >&2; "
        "exit 1; "
        "fi; "
        f"bash {remote_shell_path(install_script)}"
    )
    ssh_command = ["ssh", "-tt", remote_host, command] if interactive else [
        "ssh", "-n", "-o", "BatchMode=yes", remote_host, command
    ]
    run_or_fail(ssh_command, cwd=cwd, label="remote root install")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Copy deployment/yaha to a remote system via scp with checksum-based "
            "skip and overwrite prompts for protected INI config files."
        )
    )
    parser.add_argument(
        "--local-dir",
        default=str(DEFAULT_LOCAL_DIR),
        help="Local deployment directory to copy",
    )
    parser.add_argument(
        "--remote-host",
        default=DEFAULT_REMOTE_HOST,
        help="Remote SSH host (user@host or host)",
    )
    parser.add_argument(
        "--remote-dir",
        default=DEFAULT_REMOTE_DIR,
        help="Remote target directory",
    )
    parser.add_argument(
        "--no-overwrite-ini",
        action="store_true",
        help="Never overwrite differing protected INI config files",
    )
    parser.add_argument(
        "--install-component",
        action="append",
        default=[],
        help=(
            "After copy, run remote <component>/install.sh for this component. "
            "Repeatable, e.g. --install-component automation"
        ),
    )
    parser.add_argument(
        "--install-root",
        action="store_true",
        help=(
            "After copy, run remote root install.sh from deployment directory. "
            "This installs all component services and nginx config logic."
        ),
    )
    parser.add_argument(
        "--non-interactive-install",
        action="store_true",
        help=(
            "Run remote install commands without TTY and with SSH BatchMode. "
            "Use this only when passwordless sudo is configured."
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    local_root = Path(args.local_dir).expanduser().resolve()
    remote_host = normalize_remote_host(args.remote_host)
    remote_root = args.remote_dir

    no_overwrite_ini = args.no_overwrite_ini

    if not local_root.exists() or not local_root.is_dir():
        print(f"ERROR: local directory does not exist: {local_root}", file=sys.stderr)
        return 1

    unknown_components = sorted(
        {
            name.strip()
            for name in args.install_component
            if name.strip() and name.strip() not in KNOWN_INSTALL_COMPONENTS
        }
    )
    if unknown_components:
        known = ", ".join(KNOWN_INSTALL_COMPONENTS)
        unknown = ", ".join(unknown_components)
        print(
            "ERROR: unknown --install-component value(s): "
            f"{unknown}. Known: {known}",
            file=sys.stderr,
        )
        return 1

    files = list_local_files(local_root)
    if not files:
        print(f"ERROR: local directory is empty: {local_root}", file=sys.stderr)
        return 1

    print(f"Deploy source: {local_root}")
    print(f"Deploy target: {remote_host}:{remote_root}")

    try:
        ensure_remote_directories(
            remote_host=remote_host,
            remote_root=remote_root,
            local_root=local_root,
            cwd=PROJECT_ROOT,
        )

        stats = CopyStats()
        overwrite_mode = "ask"
        interactive_install = not args.non_interactive_install
        if no_overwrite_ini:
            overwrite_mode = "none"

        for local_file in files:
            relative = local_file.relative_to(local_root)
            remote_file = build_remote_path(remote_root, relative)

            local_hash = sha256_file(local_file)
            remote_hash = remote_sha256(
                remote_host=remote_host,
                remote_path=remote_file,
                cwd=PROJECT_ROOT,
            )

            if remote_hash is not None and local_hash == remote_hash:
                stats.skipped_identical += 1
                print(f"SKIP identical {relative.as_posix()}")
                continue

            remote_exists = remote_exists_file(
                remote_host=remote_host,
                remote_path=remote_file,
                cwd=PROJECT_ROOT,
            )

            if remote_exists and is_protected_service_config_file(relative):
                if overwrite_mode == "none":
                    stats.skipped_prompt += 1
                    print(f"SKIP protected-config {relative.as_posix()}")
                    continue

                if overwrite_mode == "ask":
                    decision = prompt_overwrite(relative)
                    if decision == "n":
                        stats.skipped_prompt += 1
                        print(f"SKIP protected-config {relative.as_posix()}")
                        continue

            copy_file(
                local_file=local_file,
                remote_host=remote_host,
                remote_file=remote_file,
                cwd=PROJECT_ROOT,
            )
            stats.copied += 1
            print(f"COPY {relative.as_posix()}")

        print(
            "DEPLOY done "
            f"copied={stats.copied} "
            f"skipped_identical={stats.skipped_identical} "
            f"skipped_prompt={stats.skipped_prompt}"
        )

        for component_name in args.install_component:
            cleaned_component = component_name.strip()
            if not cleaned_component:
                continue
            run_remote_component_install(
                remote_host=remote_host,
                remote_root=remote_root,
                component_name=cleaned_component,
                cwd=PROJECT_ROOT,
                interactive=interactive_install,
            )
            print(f"INSTALL remote component={cleaned_component}")

        if args.install_root:
            run_remote_root_install(
                remote_host=remote_host,
                remote_root=remote_root,
                cwd=PROJECT_ROOT,
                interactive=interactive_install,
            )
            print("INSTALL remote root=install.sh")

        return 0
    except Exception as error:
        print(f"DEPLOY fail error={error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
