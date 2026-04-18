GitHub PR workflow. Follow the exact steps below.

## Mandatory automation

The complete check-in + PR workflow **must** be executed via:

python test/auto_checkin_pr.py --branch <branch> --commit-message "<message>" --pr-title "<title>" --pr-body "<summary>"

Manual step-by-step execution for push / create PR / close PR is not allowed anymore.
The script is the single source of truth for retries, waits, checks, PR verification, and close/merge flow.

## Required invocation

Use this exact pattern:

python test/auto_checkin_pr.py --branch <branch> --commit-message "<message>" --pr-title "feat: <module>" --pr-body "<summary>"

Required environment variable:

No mandatory environment variable.

Authentication source for the script:

1) `GITHUB_TOKEN` (if set)
2) Existing git credential helper entry for `github.com` (default)

`gh` CLI is not used.

## Scope of the script (must be used end-to-end)

- run coverage/test gate
- commit pending changes
- push feature branch
- create PR
- verify PR was created and is open
- fast-forward merge into master
- delete local and remote feature branch
- verify PR is closed and merged

Do not replace any part of this flow with manual git or manual PR API steps.
