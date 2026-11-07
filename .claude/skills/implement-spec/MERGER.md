# Merger brief

The orchestrator copies this brief into each merger's prompt, filling every `{…}`. Mergers run one at a time: they all commit to the integration worktree. The rules below go in verbatim.

---

You are merging `{implementer branch}` ({ticket title}, {ticket pointer}) into the integration branch `{integration branch}`, in the integration worktree `{integration worktree}`. The implementer's report: {report, or a path to it}. The spec is at `{notes dir}/spec.md`.

## Rules

- **Plain commands.** Write and edit files with Write and Edit, scripts included (write `/tmp/<name>.py` with Write, then run `python3 -I /tmp/<name>.py`). Each Bash call is one plain command, run from the worktree root. Your shell refuses any call it can't prove stays inside the worktree: heredocs, loops, `&&` chains, pipes into files, `cd`, `$(…)`. Split a refused call into plain commands.
- **Waiting.** Run long commands in the foreground with a long timeout. The harness blocks `sleep`.
- **Git config is already set.** The hooks are on for every worktree. Leave `git config` alone.
- **Merge, then let the checks judge it.** Run `git merge --no-ff {implementer branch}`. The merge hook runs the repo's full checks: two branches that each pass can still break together.
- **Conflicts.** Keep both sides' intent. Two tickets that each append to the same doc section keep both additions, in ticket order. Resolve with Edit, never by picking a whole side of a file. Commit the merge through the hooks.
- **Failures are yours to fix.** When the checks fail, fix the cause on the integration branch, in a commit of its own that names both tickets.
  - **Unrelated failure:** when the failing test area neither branch touched, rerun it alone, the way the verification doc says. If it passes alone, the machine was busy: commit again, and name the test in your report.
  - **Broken hook:** a hook that fails for a reason other than the code is a broken guardrail. Stop and report it.
- **Load check.** Run the app off-screen exactly as the verification doc says (`CLAUDE.md` names it), and nothing else. The desktop is shared with the user.

## Report

Under 150 words: the merge commit, the conflicts and how you resolved each, any fix commit and what broke, any test you reran alone, and the load-check result.
