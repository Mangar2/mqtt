"""Test: MessageStore backup roundtrip and memory measurement.

Loads messageStoreTestData.mtree as the initial store snapshot, runs the
message-store in test mode (empty JSONL input), measures RSS after restore,
saves a new snapshot, and verifies it is byte-identical to the original.

Provides two guarantees:
1. Memory baseline for the loaded production dataset (regression detection
   after MessageTree optimisations).
2. Lossless persistence: save(load(file)) == file.
"""

from __future__ import annotations

import filecmp
import os
import select
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
_RELEASE_DIR = _PROJECT_ROOT / "build" / "release"
_MSGSTORE_BINARY = _RELEASE_DIR / ("yahamsgstoreclient.exe" if os.name == "nt" else "yahamsgstoreclient")
_TEST_DATA_FILE = _PROJECT_ROOT / "data" / "messageStoreTestData.mtree"
_WORKSPACE_TMP_ROOT = _PROJECT_ROOT / "build" / "test-temp"

# Seeded file uses a far-future timestamp so it is always picked as the newest snapshot.
_SEED_TIMESTAMP = "9999999999999"


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as tcp_socket:
        tcp_socket.bind(("127.0.0.1", 0))
        return int(tcp_socket.getsockname()[1])


def _run_or_raise(command: list[str], label: str) -> None:
    print(f"[build] $ {' '.join(command)}")
    process = subprocess.Popen(
        command,
        cwd=_PROJECT_ROOT,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    output_lines: list[str] = []
    if process.stdout is not None:
        for output_line in process.stdout:
            stripped = output_line.rstrip("\n")
            output_lines.append(stripped)
            print(f"[build] {stripped}")

    return_code = process.wait()
    if return_code == 0:
        return

    tail = "\n".join(output_lines[-30:]).strip()
    raise RuntimeError(
        f"{label} failed with exit code {return_code}"
        + (f"\n{tail}" if tail else "")
    )


def _ensure_msgstore_binary() -> None:
    _run_or_raise(["cmake", "--preset", "release"], "cmake configure (release)")
    # Ensure the test always executes a freshly built binary from current sources.
    _run_or_raise(
        [
            "cmake",
            "--build",
            "--preset",
            "release",
            "--target",
            "yahamsgstoreclient",
        ],
        "cmake build (yahamsgstoreclient)",
    )
    if not _MSGSTORE_BINARY.exists():
        raise RuntimeError(f"msgstore binary not found at {_MSGSTORE_BINARY}")


def _sample_rss_kib(pid: int) -> int | None:
    completed = subprocess.run(
        ["ps", "-o", "rss=", "-p", str(pid)],
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        return None
    text = completed.stdout.strip()
    if not text:
        return None
    try:
        value = int(text.split()[0])
        return value if value > 0 else None
    except (ValueError, IndexError):
        return None


def _wait_for_phase(
    process: subprocess.Popen[str],
    expected_phase: str,
    timeout_seconds: float,
) -> str | None:
    """Read process stdout until the expected handshake phase token appears.

    Returns the matching line, or None on timeout or process exit.
    """
    expected_token = f"test.handshake phase={expected_phase}"
    return _wait_for_stdout_tokens(process, [expected_token], timeout_seconds)


def _wait_for_stdout_tokens(
    process: subprocess.Popen[str],
    required_tokens: list[str],
    timeout_seconds: float,
) -> str | None:
    """Read process stdout until one line contains all required tokens."""
    if process.stdout is None:
        return None

    deadline = time.monotonic() + timeout_seconds
    stdout_fd = process.stdout.fileno()
    pending_text = ""

    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break

        readable, _, _ = select.select([stdout_fd], [], [], min(remaining, 0.5))
        if not readable:
            if process.poll() is not None:
                break
            continue

        chunk = os.read(stdout_fd, 4096)
        if not chunk:
            if process.poll() is not None:
                break
            continue

        pending_text += chunk.decode("utf-8", errors="replace")
        line_parts = pending_text.split("\n")
        pending_text = line_parts.pop() if line_parts else ""

        for line in line_parts:
            if line.strip():
                print(f"[msgstore] {line}")
            if all(token in line for token in required_tokens):
                return line

    if pending_text and all(token in pending_text for token in required_tokens):
        return pending_text

    return None


def _extract_save_file_path(stdout_line: str) -> Path | None:
    """Parse saved file path from a 'phase=saved ... file=<path>' line."""
    for token in stdout_line.split():
        if token.startswith("file="):
            raw = token[len("file="):]
            return Path(raw)
    return None


def _stop_process(process: subprocess.Popen[str] | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def run_backup_roundtrip_memory(config) -> tuple[bool, str]:
    """Load messageStoreTestData.mtree, measure RSS, save and verify roundtrip."""
    _WORKSPACE_TMP_ROOT.mkdir(parents=True, exist_ok=True)
    working_dir = Path(tempfile.mkdtemp(prefix="mqtt-yaha-msgstore-roundtrip-",
                                        dir=str(_WORKSPACE_TMP_ROOT)))
    process: subprocess.Popen[str] | None = None
    started_at = time.monotonic()

    try:
        print("[phase] build_start release yahamsgstoreclient")
        _ensure_msgstore_binary()
        print(f"[phase] build_done elapsed={time.monotonic() - started_at:.2f}s")

        if not _TEST_DATA_FILE.exists():
            return False, f"test data file not found: {_TEST_DATA_FILE}"

        # Prepare persistence directory with the seed file.
        data_dir = working_dir / "data"
        data_dir.mkdir()
        seed_path = data_dir / f"messagestore_{_SEED_TIMESTAMP}.mtree"
        shutil.copy2(_TEST_DATA_FILE, seed_path)

        # Empty JSONL input — no additional messages are ingested.
        input_file = working_dir / "input.jsonl"
        input_file.write_text("# empty\n", encoding="utf-8")

        http_port = _find_free_port()
        # Generous timeout: the 11 MB compressed tree takes a few seconds to load.
        op_timeout = max(60.0, config.timeout_seconds)

        print(f"[phase] runtime_start elapsed={time.monotonic() - started_at:.2f}s")
        process = subprocess.Popen(
            [
                str(_MSGSTORE_BINARY),
                "--test", str(input_file),
                "--test-handshake",
                "--test-http-port", str(http_port),
            ],
            cwd=working_dir,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )

        # By the time ready_for_start is emitted, store.run() (restore) has completed.
        start_line = _wait_for_phase(process, "ready_for_start", op_timeout)
        if start_line is None:
            return False, f"process did not reach ready_for_start within {op_timeout:.0f}s"
        print(f"[phase] ready_for_start elapsed={time.monotonic() - started_at:.2f}s")

        rss_after_restore_kib = _sample_rss_kib(process.pid)

        # Send load — empty JSONL adds no messages; RSS should stay flat.
        if process.stdin is None:
            return False, "process stdin unavailable"
        process.stdin.write("load\n")
        process.stdin.flush()

        end_line = _wait_for_phase(process, "ready_for_end", op_timeout)
        if end_line is None:
            return False, f"process did not reach ready_for_end within {op_timeout:.0f}s"
        print(f"[phase] ready_for_end elapsed={time.monotonic() - started_at:.2f}s")

        rss_after_load_kib = _sample_rss_kib(process.pid)

        # Trigger explicit save.
        process.stdin.write("save\n")
        process.stdin.flush()

        saved_relative: Path | None = None
        saved_line = _wait_for_stdout_tokens(process, ["phase=saved", "success=1"], op_timeout)
        if saved_line is not None:
            saved_relative = _extract_save_file_path(saved_line)

        if saved_relative is None:
            return False, "save command did not produce a valid file path in stdout"

        saved_abs = working_dir / saved_relative
        if not saved_abs.exists():
            return False, f"saved snapshot file does not exist: {saved_abs}"

        # Terminate cleanly.
        process.stdin.write("exit\n")
        process.stdin.flush()
        try:
            remaining_stdout, _ = process.communicate(timeout=15)
            if remaining_stdout:
                for line in remaining_stdout.splitlines():
                    if line.strip():
                        print(f"[msgstore] {line}")
        except subprocess.TimeoutExpired:
            process.kill()
            remaining_stdout, _ = process.communicate()
            if remaining_stdout:
                for line in remaining_stdout.splitlines():
                    if line.strip():
                        print(f"[msgstore] {line}")

        if process.returncode != 0:
            return False, f"process exited with non-zero code {process.returncode}"

        # Compare the saved snapshot byte-for-byte with the original test data file.
        # save(load(file)) must equal file because:
        #   - MTREE2 read/write is deterministic and lossless
        #   - children are stored in file-insertion order (vector)
        #   - std::quoted and double formatting are idempotent for values written
        #     by the same code
        files_identical = filecmp.cmp(_TEST_DATA_FILE, saved_abs, shallow=False)

        rss_mib = (
            f"{rss_after_restore_kib / 1024:.1f} MiB ({rss_after_restore_kib} KiB)"
            if rss_after_restore_kib is not None
            else "n/a"
        )
        rss_load_mib = (
            f"{rss_after_load_kib / 1024:.1f} MiB ({rss_after_load_kib} KiB)"
            if rss_after_load_kib is not None
            else "n/a"
        )

        summary_lines = [
            f"RSS after restore : {rss_mib}",
            f"RSS after load    : {rss_load_mib}",
            f"File roundtrip    : {'identical' if files_identical else 'DIFFERS'}",
            f"Saved snapshot    : {saved_abs}",
        ]

        print("\n".join(summary_lines))

        if not files_identical:
            return False, "saved snapshot differs from original\n" + "\n".join(summary_lines)

        return True, "\n".join(summary_lines)

    except Exception as error:
        return False, f"backup_roundtrip_memory failed: {error}"
    finally:
        if process is not None and process.poll() is None:
            try:
                if process.stdin:
                    process.stdin.write("exit\n")
                    process.stdin.flush()
            except Exception:
                pass
            _stop_process(process)
        shutil.rmtree(working_dir, ignore_errors=True)


TEST_CASES = [
    {
        "name": "yaha/msgstore/backup_roundtrip_memory",
        "description": (
            "Load messageStoreTestData.mtree, measure RSS after restore, "
            "save snapshot, verify byte-identical roundtrip"
        ),
        "run": run_backup_roundtrip_memory,
    },
]
