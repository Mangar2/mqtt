#!/usr/bin/env python3
"""Fully automated check-in + PR workflow for this repository.

Workflow:
1. Validate git context and parameters.
2. Create/switch feature branch when needed.
3. Run coverage gate (`test/run_coverage.py`) and verify success.
4. Commit pending changes.
5. Push branch and wait until remote branch is visible.
6. Create PR via GitHub REST API and verify it is open.
7. Fast-forward merge branch into master and delete branch local/remote.
8. Verify PR is closed and merged.

Authentication:
- Uses `GITHUB_TOKEN` when provided.
- Otherwise reads the existing git credential helper entry for github.com
    (same credential source used by `git push`).
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import queue
import re
import shlex
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass

COVERAGE_THRESHOLD_PERCENT = 80.0


class WorkflowError(RuntimeError):
    """Raised when a workflow step fails."""


@dataclass(frozen=True)
class Config:
    owner: str
    repo: str
    base_branch: str
    branch: str
    commit_message: str
    pr_title: str
    pr_body: str
    poll_interval_sec: float
    poll_timeout_sec: float
    skip_tests: bool
    allow_clean_tree: bool


def run_cmd(command: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    """Run shell command and return completed process.

    The repository uses PowerShell terminals, but Python subprocess with shell
    command strings works consistently for git invocations here.
    """
    command_env = dict(os.environ)
    command_env["GIT_TERMINAL_PROMPT"] = "0"
    command_env["GCM_INTERACTIVE"] = "Never"

    completed = subprocess.run(
        command,
        shell=True,
        text=True,
        capture_output=True,
        env=command_env,
        timeout=900,
    )
    if check and completed.returncode != 0:
        raise WorkflowError(
            "Command failed:\n"
            f"  {command}\n"
            f"Exit code: {completed.returncode}\n"
            f"STDOUT:\n{completed.stdout}\n"
            f"STDERR:\n{completed.stderr}"
        )
    return completed


def run_cmd_with_input(
    command: str,
    input_text: str,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    """Run shell command with stdin input and return completed process."""
    command_env = dict(os.environ)
    command_env["GIT_TERMINAL_PROMPT"] = "0"
    command_env["GCM_INTERACTIVE"] = "Never"

    completed = subprocess.run(
        command,
        shell=True,
        text=True,
        input=input_text,
        capture_output=True,
        env=command_env,
        timeout=30,
    )
    if check and completed.returncode != 0:
        raise WorkflowError(
            "Command failed:\n"
            f"  {command}\n"
            f"Exit code: {completed.returncode}\n"
            f"STDOUT:\n{completed.stdout}\n"
            f"STDERR:\n{completed.stderr}"
        )
    return completed


def log(message: str) -> None:
    """Print one status line."""
    print(f"[auto-checkin] {message}")


def log_timed(step_name: str, start_time: float) -> None:
    """Print duration line for a completed step."""
    elapsed = time.monotonic() - start_time
    log(f"{step_name} finished in {elapsed:.2f}s")


def poll_until(predicate, description: str, timeout_sec: float, interval_sec: float):
    """Poll predicate until it returns truthy or timeout expires."""
    deadline = time.monotonic() + timeout_sec
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except Exception as exc:  # noqa: BLE001
            last_error = exc
        time.sleep(interval_sec)

    if last_error is not None:
        raise WorkflowError(
            f"Timeout while waiting for {description}. Last error: {last_error}"
        )
    raise WorkflowError(f"Timeout while waiting for {description}.")


def git_current_branch() -> str:
    return run_cmd("git branch --show-current").stdout.strip()


def git_is_dirty() -> bool:
    return bool(run_cmd("git status --porcelain").stdout.strip())


def ensure_on_branch(branch_name: str) -> None:
    current = git_current_branch()
    if current == branch_name:
        return
    run_cmd(f"git checkout {branch_name}")


def ensure_git_repo() -> None:
    run_cmd("git rev-parse --is-inside-work-tree")


def ensure_origin_remote() -> None:
    result = run_cmd("git remote")
    remotes = {entry.strip() for entry in result.stdout.splitlines() if entry.strip()}
    if "origin" not in remotes:
        raise WorkflowError("Missing git remote 'origin'.")


def _python_command_candidates() -> list[list[str]]:
    candidates: list[list[str]] = []

    if sys.executable:
        candidates.append([sys.executable])

    env_python = os.environ.get("PYTHON", "").strip()
    if env_python:
        candidates.append([env_python])

    candidates.append(["python3"])
    candidates.append(["python"])

    if os.name == "nt":
        candidates.append(["py", "-3"])

    unique: list[list[str]] = []
    seen: set[tuple[str, ...]] = set()
    for candidate in candidates:
        key = tuple(candidate)
        if key not in seen:
            seen.add(key)
            unique.append(candidate)
    return unique


def _python_meets_min_version(command_prefix: list[str]) -> bool:
    check_cmd = command_prefix + [
        "-c",
        "import sys; raise SystemExit(0 if sys.version_info >= (3, 9) else 1)",
    ]
    result = subprocess.run(
        check_cmd,
        text=True,
        capture_output=True,
        env=dict(os.environ),
        timeout=10,
    )
    return result.returncode == 0


def resolve_python_command() -> list[str]:
    for candidate in _python_command_candidates():
        try:
            if _python_meets_min_version(candidate):
                return candidate
        except (FileNotFoundError, subprocess.SubprocessError):
            continue

    raise WorkflowError(
        "No compatible Python interpreter found. "
        "Need Python >= 3.9 for test/run_coverage.py."
    )


def run_coverage_gate() -> None:
    coverage_script = Path("test") / "run_coverage.py"
    python_command = resolve_python_command()
    coverage_cmd = python_command + [coverage_script.as_posix()]
    log(
        "Running coverage gate: " + shlex.join(coverage_cmd)
    )
    command_env = dict(os.environ)
    command_env["GIT_TERMINAL_PROMPT"] = "0"
    command_env["GCM_INTERACTIVE"] = "Never"

    process = subprocess.Popen(
        coverage_cmd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=command_env,
    )

    combined_lines: list[str] = []
    output_queue: queue.Queue[str | None] = queue.Queue()
    coverage_timeout_sec = 900.0
    heartbeat_interval_sec = 10.0
    step_marker = re.compile(r"^\[(\d+/\d+)\]\s+(.+)$")

    def reader_thread() -> None:
        assert process.stdout is not None
        try:
            for line in process.stdout:
                output_queue.put(line)
        finally:
            output_queue.put(None)

    thread = threading.Thread(target=reader_thread, daemon=True)
    thread.start()

    start_time = time.monotonic()
    last_output_time = start_time
    last_heartbeat_time = start_time
    current_step_name: str | None = None
    current_step_start: float | None = None
    stream_finished = False

    while True:
        now = time.monotonic()
        if now - start_time > coverage_timeout_sec:
            process.kill()
            raise WorkflowError(
                f"Coverage process timed out after {coverage_timeout_sec:.0f}s."
            )

        try:
            item = output_queue.get(timeout=0.5)
        except queue.Empty:
            if process.poll() is not None and stream_finished:
                break
            if now - last_heartbeat_time >= heartbeat_interval_sec:
                since_output = now - last_output_time
                total = now - start_time
                log(
                    "Coverage still running: "
                    f"total={total:.1f}s, since last output={since_output:.1f}s"
                )
                last_heartbeat_time = now
            continue

        if item is None:
            stream_finished = True
            if process.poll() is not None:
                break
            continue

        line = item
        combined_lines.append(line)
        sys.stdout.write(line)
        sys.stdout.flush()
        last_output_time = time.monotonic()

        stripped = line.strip()
        match = step_marker.match(stripped)
        if match:
            marker = match.group(1)
            title = match.group(2)
            new_step_name = f"Coverage step {marker} {title}"
            now_step = time.monotonic()
            if current_step_name is not None and current_step_start is not None:
                elapsed_prev = now_step - current_step_start
                log(f"{current_step_name} finished in {elapsed_prev:.2f}s")
            current_step_name = new_step_name
            current_step_start = now_step
            log(f"{current_step_name} started")

        if process.poll() is not None and stream_finished and output_queue.empty():
            break

    return_code = process.wait(timeout=5)
    if current_step_name is not None and current_step_start is not None:
        elapsed_last = time.monotonic() - current_step_start
        log(f"{current_step_name} finished in {elapsed_last:.2f}s")

    total_coverage_time = time.monotonic() - start_time
    log(f"Coverage gate finished in {total_coverage_time:.2f}s")
    output = "".join(combined_lines)

    if return_code != 0:
        raise WorkflowError(
            "Coverage script failed.\n"
            f"Exit code: {return_code}\n"
            f"Output:\n{output}"
        )

    tests_ok = re.search(r"Tests\s*:\s*\d+/\d+\s*\[OK\]", output) is not None
    if not tests_ok:
        raise WorkflowError(
            "Coverage output does not confirm successful tests.\n"
            f"Output:\n{output}"
        )

    if "Threshold  : MET" not in output:
        raise WorkflowError(
            "Coverage threshold is not MET "
            f"(required >= {COVERAGE_THRESHOLD_PERCENT:.0f}%).\n"
            f"Output:\n{output}"
        )


def checkout_or_create_branch(base_branch: str, branch: str) -> None:
    current = git_current_branch()
    if current == branch:
        return

    existing_local = run_cmd("git branch --list").stdout
    if any(line.strip().lstrip("* ").strip() == branch for line in existing_local.splitlines()):
        stashed = False
        if git_is_dirty():
            stash_result = run_cmd(
                'git stash push -u -m "auto-checkin-branch-switch"',
                check=False,
            )
            if "No local changes to save" not in stash_result.stdout:
                stashed = True
        run_cmd(f"git checkout {branch}")
        if stashed:
            run_cmd("git stash pop")
        return

    if current == base_branch:
        run_cmd(f"git checkout -b {branch}")
        return

    run_cmd(f"git checkout -b {branch}")


def commit_if_needed(commit_message: str, allow_clean_tree: bool) -> None:
    if not git_is_dirty():
        if allow_clean_tree:
            log("Working tree is clean; skipping commit as configured.")
            return
        raise WorkflowError(
            "Working tree is clean and --allow-clean-tree is not set. "
            "Nothing to commit."
        )

    run_cmd("git add -A")
    result = run_cmd(f'git commit -m "{commit_message}"', check=False)
    if result.returncode != 0:
        status_after = run_cmd("git status --porcelain").stdout.strip()
        if not status_after:
            log("No staged changes after git add; commit skipped.")
            return
        raise WorkflowError(
            "Commit failed.\n"
            f"STDOUT:\n{result.stdout}\n"
            f"STDERR:\n{result.stderr}"
        )


def push_branch(branch: str) -> None:
    run_cmd(f"git push -u origin {branch}")


def wait_remote_branch(branch: str, timeout_sec: float, interval_sec: float) -> None:
    def predicate() -> bool:
        result = run_cmd(f"git ls-remote --heads origin {branch}", check=False)
        return bool(result.stdout.strip())

    poll_until(
        predicate,
        description=f"remote branch origin/{branch}",
        timeout_sec=timeout_sec,
        interval_sec=interval_sec,
    )


def github_request(token: str, method: str, url: str, payload: dict | None = None) -> dict:
    data = None
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")

    request = urllib.request.Request(url=url, method=method, data=data)
    request.add_header("Accept", "application/vnd.github+json")
    request.add_header("Authorization", f"Bearer {token}")
    request.add_header("X-GitHub-Api-Version", "2022-11-28")
    request.add_header("User-Agent", "mqtt-auto-checkin-script")
    if data is not None:
        request.add_header("Content-Type", "application/json")

    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            body = response.read().decode("utf-8")
            if not body:
                return {}
            return json.loads(body)
    except urllib.error.HTTPError as http_error:
        error_body = http_error.read().decode("utf-8", errors="replace")
        raise WorkflowError(
            f"GitHub API error {http_error.code} for {method} {url}:\n{error_body}"
        ) from http_error


def resolve_github_token(owner: str, repo: str) -> str:
    """Resolve GitHub API token.

    Priority:
    1. `GITHUB_TOKEN` env var.
    2. `git credential fill` entry for github.com + repo path.
    """
    env_token = os.environ.get("GITHUB_TOKEN", "").strip()
    if env_token:
        return env_token

    credential_query = (
        "protocol=https\n"
        "host=github.com\n"
        f"path={owner}/{repo}.git\n"
        "\n"
    )
    result = run_cmd_with_input(
        "git credential fill",
        input_text=credential_query,
        check=False,
    )
    if result.returncode != 0 or not result.stdout.strip():
        # Fallback: some credential helpers only match on host, not path.
        fallback_query = (
            "protocol=https\n"
            "host=github.com\n"
            "\n"
        )
        result = run_cmd_with_input(
            "git credential fill",
            input_text=fallback_query,
            check=False,
        )
        if result.returncode != 0 or not result.stdout.strip():
            raise WorkflowError(
                "No GitHub API token available. "
                "`GITHUB_TOKEN` is not set and git credential helper returned "
                "no usable credentials."
            )

    fields: dict[str, str] = {}
    for line in result.stdout.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        fields[key.strip()] = value.strip()

    password = fields.get("password", "").strip()
    if password:
        return password

    # Some setups store token-like secret in username with empty password.
    username = fields.get("username", "").strip()
    if username and username != "x-access-token":
        return username

    raise WorkflowError(
        "No GitHub API token available. "
        "Set `GITHUB_TOKEN` or ensure your git credential manager has a "
        "stored github.com credential."
    )


def create_or_find_open_pr(cfg: Config, token: str) -> dict:
    create_url = f"https://api.github.com/repos/{cfg.owner}/{cfg.repo}/pulls"
    payload = {
        "title": cfg.pr_title,
        "head": cfg.branch,
        "base": cfg.base_branch,
        "body": cfg.pr_body,
        "draft": False,
    }

    try:
        return github_request(token, "POST", create_url, payload)
    except WorkflowError as exc:
        message = str(exc)
        already_exists_hint = "A pull request already exists"
        if already_exists_hint not in message:
            raise

        list_url = (
            f"https://api.github.com/repos/{cfg.owner}/{cfg.repo}/pulls"
            f"?state=open&head={cfg.owner}:{cfg.branch}&base={cfg.base_branch}"
        )
        listing = github_request(token, "GET", list_url)
        if isinstance(listing, list) and listing:
            return listing[0]
        raise


def fetch_pr(cfg: Config, token: str, pr_number: int) -> dict:
    url = f"https://api.github.com/repos/{cfg.owner}/{cfg.repo}/pulls/{pr_number}"
    return github_request(token, "GET", url)


def verify_pr_open(cfg: Config, token: str, pr_number: int, timeout_sec: float, interval_sec: float) -> dict:
    def predicate() -> dict | None:
        payload = fetch_pr(cfg, token, pr_number)
        if payload.get("state") != "open":
            return None
        head = payload.get("head", {})
        if head.get("ref") != cfg.branch:
            return None
        return payload

    return poll_until(
        predicate,
        description=f"PR #{pr_number} to be open",
        timeout_sec=timeout_sec,
        interval_sec=interval_sec,
    )


def merge_and_cleanup_via_git(cfg: Config) -> None:
    # Execute close flow step-by-step for shell portability on Windows.
    run_cmd("git fetch origin")
    run_cmd(f"git checkout {cfg.base_branch}")
    pull_ff_only = run_cmd(
        f"git pull --ff-only origin {cfg.base_branch}",
        check=False,
    )
    if pull_ff_only.returncode != 0:
        run_cmd(f"git pull --rebase origin {cfg.base_branch}")
    run_cmd(f"git merge --ff-only {cfg.branch}")
    run_cmd(f"git push origin {cfg.base_branch}")
    run_cmd(f"git branch -d {cfg.branch}")
    run_cmd(f"git push origin --delete {cfg.branch}")


def verify_pr_merged(cfg: Config, token: str, pr_number: int, timeout_sec: float, interval_sec: float) -> dict:
    def predicate() -> dict | None:
        payload = fetch_pr(cfg, token, pr_number)
        if payload.get("state") != "closed":
            return None
        if payload.get("merged_at") is None:
            return None
        return payload

    return poll_until(
        predicate,
        description=f"PR #{pr_number} to be closed and merged",
        timeout_sec=timeout_sec,
        interval_sec=interval_sec,
    )


def parse_args() -> Config:
    parser = argparse.ArgumentParser(
        description="Automate check-in, PR creation, merge, and verification."
    )
    parser.add_argument("--owner", default="Mangar2", help="GitHub owner")
    parser.add_argument("--repo", default="mqtt", help="GitHub repository name")
    parser.add_argument("--base-branch", default="master", help="Base branch")
    parser.add_argument("--branch", required=True, help="Feature branch name")
    parser.add_argument(
        "--commit-message",
        required=True,
        help="Commit message used when there are pending changes",
    )
    parser.add_argument("--pr-title", required=True, help="Pull request title")
    parser.add_argument(
        "--pr-body",
        default="Automated check-in workflow execution.",
        help="Pull request body",
    )
    parser.add_argument(
        "--poll-interval-sec",
        type=float,
        default=3.0,
        help="Polling interval in seconds for remote/PR verification",
    )
    parser.add_argument(
        "--poll-timeout-sec",
        type=float,
        default=180.0,
        help="Timeout in seconds for remote/PR verification",
    )
    parser.add_argument(
        "--skip-tests",
        action="store_true",
        help="Skip coverage/test gate (not recommended)",
    )
    parser.add_argument(
        "--allow-clean-tree",
        action="store_true",
        help="Allow workflow when there are no local changes to commit",
    )

    args = parser.parse_args()
    return Config(
        owner=args.owner,
        repo=args.repo,
        base_branch=args.base_branch,
        branch=args.branch,
        commit_message=args.commit_message,
        pr_title=args.pr_title,
        pr_body=args.pr_body,
        poll_interval_sec=args.poll_interval_sec,
        poll_timeout_sec=args.poll_timeout_sec,
        skip_tests=args.skip_tests,
        allow_clean_tree=args.allow_clean_tree,
    )


def main() -> int:
    try:
        workflow_start = time.monotonic()

        step_start = time.monotonic()
        cfg = parse_args()
        log_timed("Argument parsing", step_start)

        step_start = time.monotonic()
        token = resolve_github_token(cfg.owner, cfg.repo)
        log_timed("Token resolution", step_start)

        step_start = time.monotonic()
        log("Validating git repository context")
        ensure_git_repo()
        ensure_origin_remote()
        log_timed("Git repository validation", step_start)

        step_start = time.monotonic()
        log(f"Preparing branch '{cfg.branch}'")
        checkout_or_create_branch(cfg.base_branch, cfg.branch)
        log_timed("Branch preparation", step_start)

        if not cfg.skip_tests:
            step_start = time.monotonic()
            run_coverage_gate()
            log_timed("Coverage gate", step_start)
        else:
            log("Skipping coverage gate by explicit flag")

        step_start = time.monotonic()
        log("Committing pending changes if needed")
        commit_if_needed(cfg.commit_message, allow_clean_tree=cfg.allow_clean_tree)
        log_timed("Commit step", step_start)

        step_start = time.monotonic()
        log("Pushing feature branch")
        push_branch(cfg.branch)
        log_timed("Push step", step_start)

        step_start = time.monotonic()
        log("Waiting for remote branch visibility")
        wait_remote_branch(
            cfg.branch,
            timeout_sec=cfg.poll_timeout_sec,
            interval_sec=cfg.poll_interval_sec,
        )
        log_timed("Remote branch visibility wait", step_start)

        step_start = time.monotonic()
        log("Creating PR via GitHub API")
        pr_payload = create_or_find_open_pr(cfg, token)
        log_timed("PR create/find", step_start)
        pr_number = int(pr_payload["number"])
        pr_url = str(pr_payload["html_url"])

        step_start = time.monotonic()
        log(f"Verifying PR #{pr_number} is open")
        verify_pr_open(
            cfg,
            token,
            pr_number,
            timeout_sec=cfg.poll_timeout_sec,
            interval_sec=cfg.poll_interval_sec,
        )
        log_timed(f"PR #{pr_number} open verification", step_start)

        step_start = time.monotonic()
        log("Merging branch into base and cleaning up branch")
        merge_and_cleanup_via_git(cfg)
        log_timed("Merge and cleanup", step_start)

        step_start = time.monotonic()
        log(f"Verifying PR #{pr_number} is closed and merged")
        merged_payload = verify_pr_merged(
            cfg,
            token,
            pr_number,
            timeout_sec=cfg.poll_timeout_sec,
            interval_sec=cfg.poll_interval_sec,
        )
        log_timed(f"PR #{pr_number} merged verification", step_start)

        workflow_elapsed = time.monotonic() - workflow_start
        log(f"Total workflow runtime: {workflow_elapsed:.2f}s")

        print("\n=== SUCCESS ===")
        print(f"PR created and merged: #{pr_number}")
        print(f"PR URL: {pr_url}")
        print(f"Merged at: {merged_payload.get('merged_at')}")
        print(f"Base branch: {cfg.base_branch}")
        return 0
    except WorkflowError as err:
        print("\n=== FAILURE ===", file=sys.stderr)
        print(str(err), file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\n=== ABORTED ===", file=sys.stderr)
        print("Interrupted by user.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
