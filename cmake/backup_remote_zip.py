#!/usr/bin/env python3
from __future__ import annotations

"""Create a dated local ZIP backup from a remote directory via SSH."""

import argparse
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_DIR = PROJECT_ROOT / "deployment" / "backups"
DEFAULT_REMOTE_HOST = "pi@raspberrypi"
BENIGN_REMOTE_TAR_WARNING_PATTERNS = (
    re.compile(r"^tar: .*: file changed as we read it$"),
    re.compile(r"^tar: .*: File removed before we read it$"),
    re.compile(r"^tar: Exiting with failure status due to previous errors$"),
)


def sanitize_for_filename(value: str) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9._-]+", "_", value.strip())
    return sanitized.strip("._-") or "value"


def normalize_remote_host(remote_host: str) -> str:
    cleaned = remote_host.strip()
    if not cleaned:
        raise RuntimeError("Remote host must not be empty.")
    if "@" not in cleaned:
        return f"pi@{cleaned}"
    return cleaned


def is_only_benign_remote_tar_warnings(stderr_text: str) -> bool:
    lines = [line.strip() for line in stderr_text.splitlines() if line.strip()]
    if not lines:
        return False
    for line in lines:
        if not any(pattern.match(line) for pattern in BENIGN_REMOTE_TAR_WARNING_PATTERNS):
            return False
    return True


def stream_remote_directory_to_temp(
    *,
    remote_host: str,
    remote_start_dir: str,
    extract_target: Path,
    cwd: Path,
) -> None:
    if not remote_start_dir.strip():
        raise RuntimeError("Remote start directory must not be empty.")

    quoted_remote_dir = shlex.quote(remote_start_dir)
    remote_command = (
        f"if [ ! -d {quoted_remote_dir} ]; then "
        f"echo 'Remote directory does not exist: {remote_start_dir}' >&2; "
        "exit 1; "
        "fi; "
        f"cd {quoted_remote_dir} && tar -cf - ."
    )

    ssh_process = subprocess.Popen(
        ["ssh", "-n", "-o", "BatchMode=yes", remote_host, remote_command],
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert ssh_process.stdout is not None

    try:
        tar_result = subprocess.run(
            ["tar", "-xf", "-", "-C", str(extract_target)],
            stdin=ssh_process.stdout,
            check=False,
            capture_output=True,
            text=True,
        )
    finally:
        ssh_process.stdout.close()

    ssh_stderr = ssh_process.stderr.read().decode("utf-8", errors="replace")
    ssh_process.stderr.close()
    ssh_return_code = ssh_process.wait()

    if ssh_return_code != 0 and not is_only_benign_remote_tar_warnings(ssh_stderr):
        stderr_text = ssh_stderr.strip()
        raise RuntimeError(
            "SSH transfer failed"
            + (f": {stderr_text}" if stderr_text else "")
        )

    if ssh_return_code != 0 and is_only_benign_remote_tar_warnings(ssh_stderr):
        print(
            "Warning: remote files changed during backup; archive was created from a live snapshot.",
            file=sys.stderr,
        )

    if tar_result.returncode != 0:
        stderr_text = tar_result.stderr.strip()
        raise RuntimeError(
            "Local tar extraction failed"
            + (f": {stderr_text}" if stderr_text else "")
        )


def create_backup_archive(
    *,
    remote_host: str,
    remote_start_dir: str,
    output_dir: Path,
    cwd: Path,
) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)

    host_part = sanitize_for_filename(remote_host)
    dir_part = sanitize_for_filename(remote_start_dir)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    archive_stem = f"backup_{host_part}_{dir_part}_{timestamp}"
    archive_base = output_dir / archive_stem

    with tempfile.TemporaryDirectory(prefix="yaha_backup_") as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        stream_remote_directory_to_temp(
            remote_host=remote_host,
            remote_start_dir=remote_start_dir,
            extract_target=temp_dir,
            cwd=cwd,
        )
        archive_path = shutil.make_archive(
            base_name=str(archive_base),
            format="zip",
            root_dir=str(temp_dir),
            base_dir=".",
        )

    return Path(archive_path)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Create a dated local ZIP backup from a remote directory via SSH. "
            "The remote directory is streamed as tar and packed locally as zip."
        )
    )
    parser.add_argument(
        "--remote-host",
        default=DEFAULT_REMOTE_HOST,
        help="Remote SSH host (user@host or host)",
    )
    parser.add_argument(
        "--remote-start-dir",
        required=True,
        help="Remote start directory to back up recursively",
    )
    parser.add_argument(
        "--output-dir",
        default=str(DEFAULT_OUTPUT_DIR),
        help="Local output directory for ZIP backups",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    remote_host = normalize_remote_host(args.remote_host)
    remote_start_dir = str(args.remote_start_dir)
    output_dir = Path(args.output_dir).expanduser()

    try:
        archive_path = create_backup_archive(
            remote_host=remote_host,
            remote_start_dir=remote_start_dir,
            output_dir=output_dir,
            cwd=PROJECT_ROOT,
        )
    except RuntimeError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    print(f"Backup created: {archive_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
