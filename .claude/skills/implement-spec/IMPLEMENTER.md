# Implementer brief

The orchestrator copies this brief into each implementer's prompt, filling every `{…}`. The rules below go in verbatim: the implementer sees this file only through the brief.

---

You are implementing **{ticket title}** ({ticket pointer}) for spec {spec pointer}. The ticket body is at `{notes dir}/{ticket file}`, and the spec at `{notes dir}/spec.md`; read the sections {headings}. {Exploration notes, if any: paths.} Fetch nothing from the tracker: the notes are current.

You work alone in your own worktree, on your own branch. The integration branch is `{integration branch}`.

## Rules

- **Plain commands.** Write and edit files with Write and Edit, scripts included (write `/tmp/<name>.py` with Write, then run `python3 -I /tmp/<name>.py`). Each Bash call is one plain command, run from the worktree root. Your shell refuses any call it can't prove stays inside the worktree: heredocs, loops, `&&` chains, pipes into files, `cd`, `$(…)`. Split a refused call into plain commands; don't rephrase it into another compound one.
- **Waiting.** Run long commands (a build, the check) in the foreground with a long timeout. The harness blocks `sleep`.
- **Start on the integration branch.** Run `git merge-base --is-ancestor {integration branch} HEAD`. If it exits non-zero, run `git reset --hard {integration branch}` before anything else.
- **Git config is already set.** The hooks are on for every worktree. Leave `git config` alone.
- **Build with the `tdd` skill.** Call the Skill tool with `tdd` and work red-green: each behavior starts as a test you saw fail.
- **Run the app only as the verification doc says** (`CLAUDE.md` names it). The desktop is shared with the user, and a launch outside that recipe lands on their screen.
- **Commit through the hooks.**
  - **Unrelated failure:** when a hook fails in a test area your change doesn't touch, rerun that area alone, the way the verification doc says. If it passes alone, the machine was busy: commit again, and name the test in your report. If it fails alone, it's yours to fix.
  - **Broken hook:** a hook that fails for a reason other than the code (a missing tool, a crash in the hook itself) is a broken guardrail. Stop and report it.
- **The spec is the truth.** Write tests for what the spec and the ticket say. A behavior you had to decide yourself (a snap, a default, a threshold) goes in your report as **needs-eyes**, not into a test as fact.
- **Finish merged.** Before reporting, merge the integration branch tip into your branch (`git merge {integration branch}`) and commit the merge through the hooks.

## Report

Under 200 words:

1. **Done:** what the ticket now does, in one or two sentences.
2. **First failing test:** its name, and the failure you saw before the code existed.
3. **Check:** the result of the last hook run, plus any test you reran alone and why.
4. **Needs-eyes:** what only the user can judge on the real monitor, each item in the form Setup / Do / Expect / Where to look. Include every behavior you decided yourself.
5. **Not done:** anything in the ticket you left out, and why.
