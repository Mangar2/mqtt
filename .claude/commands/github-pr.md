GitHub PR workflow. No gh CLI. Use git + VS Code PR tool only.

## Open PR

Push branch:
git push origin <branch>

Create PR via github-pull-request_create_pull_request:
title: "feat: <module>"
head: "<branch>"
base: "master"
body: "<summary>"
draft: false
repo: { owner: "Mangar2", name: "mqtt" }

## Close PR

One terminal call:
git checkout master ; git merge --ff-only <branch> ; git push origin master ; git branch -d <branch> ; git push origin --delete <branch>

Rules:
ff-only always. No squash no rebase.
Delete local and remote same call.
Never push direct to master.

## Unstaged changes before close

git add -A ; git commit -m "<message>" ; git checkout master ; git merge --ff-only <branch> ; git push origin master ; git branch -d <branch> ; git push origin --delete <branch>
