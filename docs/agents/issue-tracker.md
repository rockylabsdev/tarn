# Issue tracker: GitHub

Issues and specs for this repo live as GitHub issues. Use the `gh` CLI for all operations.

## Conventions

- **Create an issue**: `gh issue create --title "..." --body "..."`. Use a heredoc for multi-line bodies.
- **Read an issue**: `gh issue view <number> --json number,title,body,labels,comments`. `--comments` and `--json` don't combine (gh refuses): comments come through `--json comments`. A spec runs to 50 KB, so fetch it once (`--json body --jq .body`), save it to a notes file, and read sections from there; fetch comments only when the body points at them.
- **List issues**: `gh issue list --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'` with appropriate `--label` and `--state` filters.
- **Make an issue a sub-issue of a parent**: `gh issue create --parent <parent> ...`, or `gh issue edit <parent> --add-sub-issue <child>` afterwards (`gh` 2.94+). Older `gh`: `gh api --method POST repos/<owner>/<repo>/issues/<parent>/sub_issues -F sub_issue_id=<child-db-id>` (database id, as in **Blocking** below). Without sub-issues, put `Part of #<parent>` at the top of the child body.
- **Comment on an issue**: `gh issue comment <number> --body "..."`
- **Apply / remove labels**: `gh issue edit <number> --add-label "..."` / `--remove-label "..."`
- **Close**: `gh issue close <number> --comment "..."`

Infer the repo from `git remote -v`; `gh` does this automatically when run inside a clone.

## Closing work: one pull request per spec

A spec's build closes through a single pull request from its integration branch to `main`, never by merging by hand. Ticket branches merge into the integration branch locally, with no PR each.

- **Open** it as a draft once the integration branch has its first merge: `gh pr create --draft --base main --head <integration-branch>`, body written with the `pr` skill, ending in one `Closes #<n>` per issue, the spec and every ticket (`Closes #12, Closes #15`; GitHub closes only the first number of `Closes #12, #15`).
- **Verification**: a PR comment gathering what each ticket checked, with real-speed motion and anything needing the user's screen marked **needs-eyes**.
- **Tickets** get a comment linking the PR when their branch merges, and stay open: the PR's merge closes them.
- **Ready for review** once every ticket and the code-review fixes are merged. The user runs any live switch or Check from the branch, records it on the PR, and merges it.

## Pull requests as a triage surface

**PRs as a request surface: no.** _(Set to `yes` if this repo treats external PRs as feature requests; `/triage` reads this flag.)_

When set to `yes`, PRs run through the same labels and states as issues, using the `gh pr` equivalents:

- **Read a PR**: `gh pr view <number> --comments` and `gh pr diff <number>` for the diff.
- **List external PRs for triage**: `gh api --paginate 'repos/{owner}/{repo}/pulls?state=open' --jq '.[] | select(.author_association | IN("OWNER","MEMBER","COLLABORATOR") | not) | {number, title, author: .user.login, author_association, labels: [.labels[].name]}'`.
- **Comment / label / close**: `gh pr comment`, `gh pr edit --add-label`/`--remove-label`, `gh pr close`.

GitHub shares one number space across issues and PRs, so a bare `#42` may be either: resolve with `gh pr view 42` and fall back to `gh issue view 42`.

## When a skill says "publish to the issue tracker"

Create a GitHub issue.

## When a skill says "fetch the relevant ticket"

Read it as in **Read an issue** above.

## Wayfinding operations

Used by `/wayfinder`. The **map** is a single issue with **child** issues as tickets.

- **Map**: a single issue labeled `wayfinder:map`, holding the Notes / Decisions-so-far / Fog body. `gh issue create --label wayfinder:map`.
- **Child ticket**: an issue linked to the map as a GitHub sub-issue (see **Make an issue a sub-issue of a parent**). Where sub-issues aren't enabled, add the child to a task list in the map body and put `Part of #<map>` at the top of the child body. Labels: `wayfinder:<type>` (`research`/`prototype`/`grilling`/`task`). Once claimed, the ticket is assigned to the driving dev.
- **Blocking**: GitHub's **native issue dependencies**, the canonical, UI-visible representation. Add an edge with `gh api --method POST repos/<owner>/<repo>/issues/<child>/dependencies/blocked_by -F issue_id=<blocker-db-id>`, where `<blocker-db-id>` is the blocker's numeric **database id** (`gh api repos/<owner>/<repo>/issues/<n> --jq .id`, _not_ the `#number` or `node_id`). GitHub reports `issue_dependencies_summary.blocked_by` (open blockers only, the live gate). Where dependencies aren't available, fall back to a `Blocked by: #<n>, #<n>` line at the top of the child body. A ticket is unblocked when every blocker is closed.
- **Frontier query**: `scripts/frontier.sh <map>` prints the map body, then its open, unblocked, unclaimed children in map order (one `number<TAB>type<TAB>title` line each; the first line wins). It reads the map's own sub-issues, never all open issues: the repo also holds build tickets, which are not on the frontier. Run it as the session's first call instead of re-deriving the query.
- **Claim**: `gh issue edit <n> --add-assignee @me`, the session's first write.
- **GitHub hiccups**: `GraphQL: Something went wrong`, an HTTP 500 or `unexpected end of JSON input` from `gh issue create` or `gh api` is GitHub's side and passes within a minute. Retry up to three times, about 20 s apart. The harness blocks a foreground `sleep`, so run the retry in the background (`sleep 20; gh …`) or wait with Monitor. before re-running a creation, list the parent's `sub_issues` to see whether the failed call half-landed. Never capture an id from a call that may have failed: `cid=$(gh api … --jq .id)` stores the error body as the id and every later step inherits it.
- **Resolve**, in this order, all in the same session:
  1. `gh issue comment <n> --body "<answer>"`, then `gh issue close <n>`, then `gh issue edit <n> --remove-assignee @me`.
  2. Append a context pointer (gist + link) to the map's Decisions-so-far.
  3. Commit and push what the ticket changed in the repo (`GLOSSARY.md`, an ADR, `docs/`) to main, with the ticket's name in the message, from a worktree of your own (CLAUDE.md, Git): `git worktree add /tmp/tarn-<n> -b ticket/<n> origin/main`, commit there, then `git push origin HEAD:main`. A docs-only commit passes the check without building. Resolution comments may link only to what's pushed.
  4. Remove the ticket's prototype worktree once its branch is pushed (`orca-ide worktree rm`, never plain `git worktree`).
  5. Remove your worktree (`git worktree remove /tmp/tarn-<n>`) and its branch, then report the resolution and what was pushed.
- **Keep the map body small.** Every session loads it whole. Decisions-so-far holds one line per decision, currently true. When a decision is reopened or overturned, replace its line instead of striking it through, and post the history as a comment on the map.
- **Build tickets** spawned from a map's spec carry `ready-for-agent` only while they're truly takeable: a parked or superseded build ticket gets a native `blocked_by` edge on what it waits for, so the frontier and agents both skip it.
