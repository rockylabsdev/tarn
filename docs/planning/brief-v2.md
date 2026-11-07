# Tarn v2: brief for the wayfinder map

Follows `docs/planning/brief.md` (the v1 brief) and the v1 spec (issue #26). Written 2026-11-03, with v1 mapped and specced and its build tickets open.

## The idea

Deepen the capture-annotate-share loop that v1 built, without opening a new front. v2 adds one new window (History), makes the Overlay the single way in, and gives the Editor the annotation and framing pieces that documentation shots keep wanting. The Recording Editor (earlier called Studio Mode) and scrolling capture are separate initiatives, planned after v1.

## Where the candidates come from

Two pools, reconciled here:

- The v1 spec's **Out of scope** and **Later, not v1** lists (issue #26), which the grillings #7, #8, #10, #11, #16 and #22 fed.
- A brainstorm on 2026-11-03 of what CleanShot has that the v1 brief never mentioned, and what a Linux tool can do that CleanShot can't.

Every deferred item is placed below, in the core, the second tier, or the cut list, so the spec's lists can be retired by this brief.

## Core: what v2 is about

Each of these touches the daily loop and builds on a v1 part that already exists.

1. **All-in-one capture mode.** One shortcut opens the Overlay and the Record panel, Timer and Text capture are pickable inside it, rather than each having its own bind. Folds in:
   - **Freeze-screen mode:** stay in the frozen Overlay after a shot and draw another Selection on the same frozen image, for a sequence of shots whose content must not change.
   - **Per-app previous area:** Shift+Print repeats the last area taken in the focused app's window class, not just the last area overall. Needs the window class from Hyprland at capture time.
   - Open question: do Print, Ctrl+Print and the Timer bind stay as direct entries alongside the all-in-one shortcut.
2. **The History window.** Browse, filter, restore, and from it: Pin, Edit, Copy, Save as. Carries the deferred items that hang off it:
   - **Pin from History.**
   - **Searchable History by recognized text:** Text capture's OCR pass runs over kept originals (lazily, or on demand) and the window searches it.
   - **A re-editable project file:** Annotations and Frame kept beside the original so a capture can be reopened in the Editor with everything still movable. The History folder is the natural home.
   - Needs a decision on where per-capture metadata lives (window title and class, OCR text, project file), since v1 History is "untouched originals" and a sidecar or index is new.
3. **Callouts and curved arrows.** Both are annotation-Handle work and should be mapped together:
   - **Callouts with leader lines** as a Counter extension: drag from a placed Counter to pull out a line ending in a text box that inherits the number. Renumbering, styles and start values stay in Counter. The leader anchors to the label's edge and stays attached when either end moves.
   - **Curved arrows** (deferred in #11).
4. **Smart window naming.** The deferred file-name pattern Setting and default-format Setting, with tokens for the focused window's title and app class. One Settings-file effort; the same metadata feeds the History window.
5. **Color picker.** A mode in the Overlay (not a Magnifier feature, per the Magnifier decision in #20) that copies the pixel under the pointer as hex and rgb.

## Second tier: after the core, if the map agrees

- **Watermark** as a Preset field: a small image or text at a chosen corner with opacity, riding along with the auto-applied Preset. Add image already covers placing a signature by hand.
- **Saved colors** in the Editor (deferred in #11).
- **Pin Lock Mode** (clicks pass through; deferred in #10) and **pinning a GIF Recording** as a looping Pin. Lock Mode needs a way back: a global toggle for every Pin, or a hover edge that stays live.
- **Converter controls** (quality, dimensions, audio) and **GIF to MP4 and back**, in the Trim window (deferred in #8).
- **Text capture extensions** sharing one OCR pass: table-aware paste as TSV, QR codes and link detection, a second "keep every line break" key (deferred in #7).
- **Island:** the Countdown on Island, and pause, restart and delete from Island (deferred in #7 and the spec). Depends on where Island's activities are by then.

## Cut from v2, or deferred again

Cheap to re-add later; none changes the architecture.

- **Show keystrokes.** Needs a privileged helper reading `/dev/input` (research #24). Hold until the Recording Editor gives it a reason; Ripples cover "show what I did" for most clips.
- **Settings window.** #22 concluded the file is enough for about ten values; v2 adds perhaps three.
- **Translate-on-capture** and **automatic language detection.** Needs a model or a service, for captures that are in English.
- **Window recording** and a **window Timer.** The interesting version is follow-the-window recording, which wants its own look at Hyprland's IPC.
- **Window-chrome Frame** (a drawn title bar around a shot) in place of device bezels. Nothing depends on it.
- Small conveniences with no shared machinery: keys on the Thumbnail, a list of recently closed Thumbnails, the Highlighter snapping to text, Crop snapping to edges inside the picture, inserting a new screenshot into the Editor, rebinding keys inside Tarn's windows, a Timer delay per shot, text capture from the Editor or a Pin.

Still out, as in v1: cloud upload and hosted share links, drag-out, hiding desktop icons, packaging for other people.

## What exists already

Everything v2 touches has a v1 home: the Overlay and its modes, the Capture store and History folder, Counter and Handles, the Presets file, the settings file, the Pin window, the Trim window, and Text capture's tesseract pass. Hyprland's IPC (`hyprctl activewindow`, `clients`) supplies window title and class.

## Possible ties to Island

The Island bridge (#60) already carries Recording state. The History window and the Countdown are the likely next things Island could show.

## Open from the start

- Whether the core's five items are the right size for one map, or whether the History window is its own.
- Where per-capture metadata lives, and whether History stays "untouched originals".
- Whether all-in-one mode replaces the v1 binds or sits beside them.
- Which second-tier items the map promotes once the core is specced.
