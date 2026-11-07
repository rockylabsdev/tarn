# QML prototypes

Read when a `wayfinder:prototype` ticket needs a UI prototype. Tarn's UI is Qt 6 QML, so its prototypes are QML scenes run with `qml6`, not web pages. The `prototype` skill's UI.md is written for web routes: take its idea (variants side by side, a switcher) from there, and the mechanics from here.

## The scaffold

A prototype lives on a branch `prototype/<name>`, which is pushed and never merged. Give it the same files each time, under `prototypes/<name>/`:

- `Theme.qml`: the Palette file (`palette_file`, a flat JSON of color names), read the way `src/core/palette/palette.cpp` reads it, standing in for the daemon's `theme`.
- `Style.qml`, `Capsule.qml`, `IconBtn.qml`, `MenuPill.qml`, `Tip.qml`, `Sep.qml`, `Icon.qml`: copies of Tarn's parts from `src/ui/qml`, with `theme.` swapped for `Theme.`. Copy them fresh from `src/ui/qml` when the real ones have changed since.
- `Scene.qml`: made-up app windows to capture.
- `run.sh` (one live window) and `shoot.sh` (off-screen to a PNG); `README.md` (how to run, the variants, the captures).

## Steps

1. **Worktree, through Orca:** `orca-ide worktree create --repo path:"$(dirname "$(git rev-parse --path-format=absolute --git-common-dir)")" --name prototype-<name> --base-branch main --issue <ticket> --setup skip --json` (the `--repo` path is the main Tarn checkout, found from any worktree of it). Remove it with `orca-ide worktree rm` once the branch is pushed (`docs/agents/issue-tracker.md`, Resolve).
2. **One QML entry point** that takes `live <variant> <state> <home>` or `shot <variant> <state> <out.png> <home>` after `--`, as `History.qml` does. Icons and Geist come from the repo's `src/ui/assets`.
3. **Off-screen shots**, which put nothing on the user's desktop: `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_XHR_ALLOW_FILE_READ=1 qml6 <Entry>.qml -- shot …` (your `shoot.sh`). `QML_XHR_ALLOW_FILE_READ=1` lets the prototype read the palette files; Tarn itself never reads files from QML. For `console.log` output from an off-screen run, add `QT_FORCE_STDERR_LOGGING=1`.
4. **Look at sheets, not single shots.** Render every state of a variant into one contact sheet (`shots/sheet-A.png`) and read the sheets. One image per state per variant cost earlier sessions 13 image reads and 2.5 MB.
5. **The user runs it live.** `run.sh <variant> [state]` opens one real window. A Tarn-like window rule (float, centered, no animation) is added once per session through `hyprctl eval`, as `run.sh` does. The README says how to launch it, so the user can try it without asking.
