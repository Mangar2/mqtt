GitHub PR workflow. Follow the exact steps below.

## Open PR

Step 1: Push branch
git push origin <branch>

Step 2: Create PR via github-pull-request_create_pull_request
title: "feat: <module>"
head: "<branch>"
base: "master"
body: "<summary>"
draft: false
repo: { owner: "Mangar2", name: "mqtt" }

Step 3: Do not verify PR creation.
No follow-up check calls. No search calls. No fetch calls.
Use the result of the create call as final.

## Close PR

Execute this exact terminal call:
git checkout master ; git merge --ff-only <branch> ; git push origin master ; git branch -d <branch> ; git push origin --delete <branch>

## Unstaged changes before close

Execute this exact terminal call:
git add -A ; git commit -m "<message>" ; git checkout master ; git merge --ff-only <branch> ; git push origin master ; git branch -d <branch> ; git push origin --delete <branch>
