# tarn

A screenshot and screen-recording tool for Hyprland on CachyOS: capture, annotate, record and share. It is a Linux tool built for Wayland.

Target machine: a desktop with one 2560x1440 monitor, running Hyprland (Wayland) on CachyOS. The user is new to Linux, so explain Linux-specific choices plainly.

## Planning

The project is planned with `/wayfinder`. The starting briefs are `docs/planning/brief.md` and `docs/planning/brief-v2.md`, and the specs built from them are in `docs/planning/` too (see its README). A UI prototype is QML: `docs/agents/prototype-qml.md`.

Before publishing a spec (`/to-spec`), test every story against `docs/hyprland.md`, `docs/recording.md` and the rules of the spec before it. An earlier spec's stories missed three: the Recording bar sits outside the recorded area, so a full-screen Recording had none; Super+Q closes a window without telling Tarn; GIF encoding ran at normal priority.

## Hyprland and recording

Before changing what Tarn asks of Hyprland or gpu-screen-recorder, read `docs/hyprland.md` (Lua API, rule-pattern and pointer traps) or `docs/recording.md` (recorder signals and start-up, GIF quality, Trim).

## Verification

How to build, check, and run the daemon off-screen (and what only the user checks on the real monitor): `docs/agents/verify.md`. Each feature's test seams: `docs/agents/features/`.

## Code map

Which module does what, and the path a capture takes through them: `docs/agents/map.md`.

## Git

Other sessions work in this checkout at the same time, and switch its branches. Commit from a worktree of your own: `git worktree add /tmp/tarn-<topic> -b <branch> origin/main`. In the shared checkout, read only: no branch switch, reset, commit or `git add`.

## Agent skills

### Issue tracker

Issues, specs and wayfinder maps live in GitHub Issues on `rockylabsdev/tarn`, via the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Default vocabulary: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `GLOSSARY.md` and `docs/adr/` at the repo root, created as terms and decisions get settled. See `docs/agents/domain.md`.
