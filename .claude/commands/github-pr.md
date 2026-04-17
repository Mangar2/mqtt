Workflow for creating and closing GitHub Pull Requests in this repository. No `gh` CLI available — use git commands and the VS Code PR tool only.

## Open a PR (new branch → PR)

Single action: one `run_in_terminal` call + one tool call.

**Step 1 — push branch** (combine with any final commit if needed):

```sh
git push origin <branch>
```

**Step 2 — create PR** via `github-pull-request_create_pull_request`:

```
title: "feat: <module description>"
head:  "<branch>"
base:  "master"          # always master
body:  "<summary of changes>"
draft: false
repo:  { owner: "Mangar2", name: "mqtt" }
```

## Close a PR (merge + cleanup)

Single action: one `run_in_terminal` call with all commands chained.

```sh
git checkout master ; git merge --ff-only <branch> ; git push origin master ; git branch -d <branch> ; git push origin --delete <branch>
```

Rules:
- Always fast-forward only (`--ff-only`) — never squash or rebase.
- Delete both the local and remote branch in the same command.
- Never push directly to `master` without merging a branch first.

## Unstaged changes before closing

If there are unstaged changes, commit them first **in the same terminal call**:

```sh
git add -A ; git commit -m "<message>" ; git checkout master ; git merge --ff-only <branch> ; git push origin master ; git branch -d <branch> ; git push origin --delete <branch>
```
