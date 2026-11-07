# Contributing to Tarn

Tarn is a screenshot and screen-recording tool for Hyprland. This page covers how to build it, report a bug and send a change. Development before v1.0.0 happened in a private repo; planning is public from here on.

## Build and check

Install the packages the README's build section lists, then from the repo root:

```sh
git config core.hooksPath .githooks   # once per clone
scripts/check.sh                      # configure, build, run every test
```

`scripts/check.sh` is the one check. The git hooks run it before a commit and refuse a push of commits that haven't passed it, and CI runs the same script on every pull request, so a problem shows up before review. It runs at low priority, so it won't make your desktop lag. Tests run without a display; `docs/agents/verify.md` explains how, and which things only a person on a real monitor can check.

Edits to Markdown files, `docs/` and `.claude/` don't need a build: the check passes straight away when only those differ from a commit that passed.

## Report a bug

Open an issue with the "Bug report" form. It asks for:

- the output of `tarn doctor`, which checks your setup and lists what is missing;
- the output of `tarn --version`;
- your Hyprland version and your GPU;
- what happened, and the steps that lead to it.

## Send a change

- **A bug fix:** open a pull request against `main`. One change per pull request.
- **Anything bigger, or a new feature:** open an issue first with the "Feature idea" form (the problem first, then your idea), so you don't build something that won't land. A pull request for an issue that exists is welcome.
- Run `scripts/check.sh` before you push. `CODING_STANDARDS.md` has the code rules and `GLOSSARY.md` the product's words (Selection, Recording, Overlay and so on).
- A pull request is never the place to propose a feature: ideas come as issues.

Be kind. Threads that aren't get closed or locked.

To report a security problem privately, see `SECURITY.md`.

## How the repo is planned

Bigger work is planned in four steps. Each leaves a record. You don't need to use them to contribute.

1. **Brief:** a short document saying what to build and why.
2. **Map:** an issue, labeled `wayfinder:map`, that holds the decisions made so far and the open questions. Its child issues are research, prototype and decision tasks.
3. **Spec:** one issue describing the finished work: the problem, user stories and decisions.
4. **Tickets:** issues, one per slice of the spec, small enough for one person or agent to build and check.

The first two briefs and the specs for v1, v2 and public v1 are kept as history in [`docs/planning/`](docs/planning/): [`brief.md`](docs/planning/brief.md) and [`brief-v2.md`](docs/planning/brief-v2.md) are good examples. Later maps, specs and tickets are public on this repo's issue tracker. Hard-to-reverse decisions are in `docs/adr/`.

## Using an agent

Optional. `CLAUDE.md` and `.claude/skills/` set up AI coding agents (written for Claude Code) to follow this repo's rules: a map of the code in `docs/agents/map.md`, how to verify in `docs/agents/verify.md`, and skills for planning and review. If you don't use an agent, ignore them; you can read the same documents yourself. Third-party skill notices are in `THIRD-PARTY.md`.

## Labels

| Label | Meaning |
|---|---|
| `needs-triage` | A maintainer hasn't looked at this yet. The issue forms add it. |
| `needs-info` | Waiting on the reporter for more detail. |
| `ready-for-agent` | Fully specified: anyone can take it, with or without an agent. |
| `ready-for-human` | Needs a person, for judgment or something only a person can check. You're welcome to take it. |
| `wontfix` | Won't be done. |
| `wayfinder:*` | Planning issues: `wayfinder:map`, and `wayfinder:research`, `prototype`, `grilling` or `task` for its children. |
