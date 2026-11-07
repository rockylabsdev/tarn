---
name: implement-spec
description: "Implement the result of /to-spec and /to-tickets in code."
disable-model-invocation: true
---

You have been provided a spec. This spec should have tickets associated with it, describing how to implement the spec.

The issue tracker should have been provided to you. If not, tell the user to run `/setup-matt-pocock-skills`.

The goal is the entire spec implemented on a single **integration branch**, with every ticket resolved the way the issue tracker closes work.

The tickets are not a list of steps. They are a **task graph** with blocking relationships between them. This means there is always a **frontier** of tickets which are ready to be grabbed.

Communication to and from subagents should be sparse. Communicate primarily through **context pointers**: to the spec, tickets, research notes, and previous commits. Don't duplicate information already available via pointers.

**Implementer subagents** run in the background, **at most three at a time**. Each one runs the repo's full checks several times, and seven at once loaded the machine until timing tests failed for no reason in the code. The user's desktop runs on the same machine.

The checkout this session started in is **shared**: other sessions use it too, and switch branches in it. Do all git work in worktrees of your own. In the shared checkout, read only: no branch switch, reset, commit or `git add`.

## Steps

1. Read the spec and tickets to understand the task graph. Save the spec and each ticket body once as markdown in a **notes directory** outside the repo, and hand subagents those paths and the section headings they need, so no subagent fetches the spec from the tracker again.

2. (optional) Use an **exploration subagent** to conduct any exploration required by the tickets - relevant codebase files or external documentation. Ensure the exploration subagent can save files - it should save its markdown notes in a directory outside the repo, accessible by all future subagents. This lets **implementer subagents** focus on implementation rather than exploration.

3. Create the integration branch from the main branch's tip, in its own **integration worktree** outside the shared checkout (`git worktree add <path> -b <branch> origin/main`). If the issue tracker closes work through PRs, or the user asks for one, open a draft PR after the first merge in step 5 (a branch with no commits ahead of main can't open one), marked as closing the spec and tickets.

4. Use **implementer subagents** to implement each ticket, each in its own worktree on its own branch. Brief each one with [`IMPLEMENTER.md`](IMPLEMENTER.md): fill its blanks with pointers and copy its rules in verbatim. When an implementer reports a broken **guardrail** (a hook that fails for a reason other than the code), fix it on the integration branch before dispatching anything else.

5. Once an **implementer subagent** completes, merge its work to the integration branch with a **merger subagent**, briefed with [`MERGER.md`](MERGER.md). Mergers run **one at a time**, because they all commit in the integration worktree. Implementers keep running alongside them, within the cap.

6. If this changes the **frontier** of available tickets, kick off more **implementer subagents** for the new tickets, up to the cap.

7. Once all tickets are complete, call the Skill tool with `code-review` on the integration branch. Group the findings by the files they touch. Give each group whose files no other group touches its own **implementer subagent**, briefed with `IMPLEMENTER.md` (the findings stand in for the ticket), up to the cap. Merge each one as in step 5.

8. If a draft PR exists, mark it ready for review. Otherwise, resolve each ticket the way the issue tracker closes work, and report the integration branch. A follow-up ticket raised during the build that changed a spec rule edits the spec's own line to the new rule, linking the ticket, so the spec never says something the code no longer does.

9. Clean up every worktree you created, the integration worktree included, once its branch is pushed.
