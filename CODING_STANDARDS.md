# Coding standards

Read at review. Each rule here is a judgment call; rules a script can decide are in `scripts/check.sh` instead, listed first so the review spends no time on them.

## Already enforced by the check

The review takes these as given, and flags only a change that weakens one:

- **American spelling** (`scripts/text-lint.sh`), in code, UI, docs and names stored on disk: `color`, `center`, `gray`, `behavior`, `canceled`. A reader of an older saved name lives in one place per file format, and that file is exempt. Third-party text stays verbatim.
- **Text a public repo shouldn't hold** (`scripts/text-lint.sh`): paths into the home folder of the machine running the check, personal email addresses, and other products' names outside `docs/planning/`.
- **QML raw values** (`scripts/qml-lint.sh`): sizes, spacing, radii, colors and durations come from `src/ui/qml/Style.qml`. Existing files are held at their count in `scripts/qml-raw-values.baseline`. **A diff that raises a line in that file is a finding**: the new value belongs in Style.
- **Slow programs run async:** tesseract, ffmpeg, ffprobe, gifski, gpu-screen-recorder and `nice` go through `ProcessRunner::runAsync`.
- **Tests use the daemon's clock**, `env->now()`, never the real one.
- **Hyprland's pointer is a touchpad to Qt:** every QML `WheelHandler` names `acceptedDevices` (`scripts/qml-lint.sh`), and tests send wheel events through `tarn::test::sendWheel`.
- **Hyprland forms:** the `hyprctl` test stub rejects pre-Lua `dispatch`, `keyword`, and rule patterns not anchored with `^…$`.
- **Windows Hyprland closes:** every `src/ui/*_windows.cpp` handles `QWindow::visibleChanged`. The rule below asks for what the check can't see.

## Windows, popovers and lists

The user found these three on the real monitor in v2, after the review had passed them.

- **A window Hyprland closes acts as if the user closed it.** Super+Q hides a window without asking Tarn. On a hide, the session ends the way its own close button ends it, so the next Edit, Pin or Trim opens a fresh window. A new window type comes with a UI test that calls `view->close()` and then opens it again (for example `aWindowClosedByTheCompositorClosesThePin` in `tests/pin_ui`).
- **Every popover, dropdown and menu** opens and closes from a press on its own button, and closes on Esc, on a press outside, and when its window hides. It highlights the row under the pointer. In the Editor, build it on `Popover.qml`, which does the first three. A new one comes with a UI test of the toggle and of Esc.
- **List models change only their changed rows**: `beginInsertRows`, `beginRemoveRows`, `dataChanged`. `beginResetModel` is for when the whole set changes (a filter or search). A reset rebuilds every delegate: in the History window, each Delete took about 4 s to show.

## The daemon never waits

The daemon owns every surface, so anything that blocks it freezes the Overlay, the Thumbnails and the Recording bar together. Beyond the programs the check names, flag blocking work that can take more than a frame or two: a large file read or write, image encoding of a full-size capture, a wait on a socket or process. Those go async (`runAsync`, a `QProcess` with signals, a worker thread). The blocking `ProcessRunner::run` is for the millisecond queries: `hyprctl`, `grim`, `notify-send`, the clipboard helper.

## Reuse before writing

v1's parallel tickets each wrote their own TOML↔QVariant conversion, monitor sizing and `hyprctl` queries, and the review had to merge them. Before a new helper, look for one:

- TOML values: `src/core/toml/toml_variant.h`.
- A window's opening size on the monitor under the pointer: `src/core/editor/opening_size.h`.
- Anything asked of Hyprland: `HyprlandBridge` (`src/core/hyprland/`), one private helper per query.
- QML controls: the parts in `src/ui/qml/` (`IconBtn`, `Capsule`, `MenuPill`, `Tip`, …), not a new look for an existing control.

Flag a new function whose job an existing one already does, or two hunks in the diff with the same shape.

## The glossary's words

Names in code, tests, commit messages and UI text use `GLOSSARY.md`'s terms, and avoid its _Avoid_ words: a Thumbnail is not a "popup", an Annotation is not a "shape", a Selection is not a "region". Read the glossary, then check every name the diff adds, and every new UI string, against it. Flag a new name for a concept the glossary already names.

No script can do this: an _Avoid_ word is banned only for the concept its entry names. "Crop", "Restore" and "Trim" are terms of their own, yet each is on another term's _Avoid_ list.

## Motion

- **Keyboard-opened UI appears instantly**, with no open animation. A fade on a shortcut-opened popup felt less snappy to the user. The `no_anim` layer rule covers the compositor; flag a QML open transition.
- **Controls change state instantly**: hover, press, toggle and the pause/play swap have no crossfade. Style deliberately has no duration for them.
- **Things that move use `Spring.qml`** (slides, the Thumbnail stack), so a second change mid-move bends the motion instead of restarting it. Flag a fixed-duration `NumberAnimation` on a position.

## Hyprland and the recorder

A change to what Tarn asks of Hyprland or gpu-screen-recorder follows `docs/hyprland.md` and `docs/recording.md`. Flag one that contradicts them without updating them.
