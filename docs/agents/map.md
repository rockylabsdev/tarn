# Where things live

One line per module, and the path a capture takes through them. Each module's own header says the rest. A feature's test seams are in `docs/agents/features/`, and the words are `GLOSSARY.md`'s.

## The capture flow

The bugs v2's review found sat at these hand-offs (the kind always passed as "area", a GIF Pin with no capture, a History id set only on leaving the Overlay). Follow the whole path when a change touches one step:

```text
key or `tarn` command → daemon (control socket) → module command
  capture fullscreen        capture/capture_fullscreen.cpp
  area, window, Sequence…   overlay/: Overlay opens on a frozen shot, then deliver()
    → ThumbnailStack::hideForCapture       Capture hide: Thumbnails off screen until the capture is on the stack
    → ScreenGrabber (grab/, grim -t ppm)   the shot's pixels; the kept PNG is made in the background (storage/png_encoder)
    → History::record (history/)           the original + its Description (<id>.png + <id>.json)
    → ThumbnailStack::add (thumbnails/)    a Thumbnail
    → ThumbnailStack::setHistoryId         links the two; writes the auto-applied Preset's Project file
    → ThumbnailStack::setOrigin            where it came from on the monitor (Pins open there)
  Recording                 recording/: RecordingSession → ThumbnailStack::addRecording (no History entry)

From a Thumbnail:
  Edit  → Editor → EditorSession (editor/)  a closing that keeps the edit writes the Project file (History::saveProject)
  Pin   → Pins (pin/)                       Hyprland rules through HyprlandBridge
  Trim  → Trim (trim/)                      Recordings only
  Copy  → Clipboard (clipboard/, tarn-clip)
  Save  → History::saveImage / addSavedPath  the Description records each saved path
History window (history_window/) reads the History store and follows its entryAdded/Changed/Removed events.
```

`src/app/main.cpp` is the `tarn` binary: the daemon, the one-request client, and `tarn doctor` (`doctor.cpp`), which runs in the client with no daemon (tests: `tests/doctor`, the real binary under the stubs).

## src/core: one folder per module (a daemon service each, `daemon.service<T>()`)

- `annotations/`: the drawing tools, one file per kind (`RegisterAnnotationKind`).
- `capture/`: the full-screen capture command.
- `clipboard/`: Copy, through the `tarn-clip` helper (`helper/`).
- `daemon/`: the `Daemon`, its control socket and module registry (`TARN_MODULE`).
- `document/`: the Capture document (shot + Annotations), Crop, undo, the Project file.
- `editor/`: the Editor service and `EditorSession` (one per open Editor), split by concern (`editor_session_*.cpp`).
- `frame/`: the Frame, its Background kinds and the Watermark.
- `grab/`: the Screen grabber (grim).
- `history/`: the History store (`~/.local/share/tarn/history`).
- `history_window/`: the History window service and its list model.
- `hyprland/`: `HyprlandBridge`, everything Tarn asks of Hyprland.
- `shell/`: the Shell socket (`docs/shell-socket.md`).
- `notify/`: desktop notifications (notify-send).
- `overlay/`: the Overlay: Modes, Selection, window shots, Last area and `capture repeat` (Shift+Print).
- `palette/`: the colors, from the Palette file.
- `pin/`: Pins and Locked Pins.
- `preset/`: Presets (`presets.toml`).
- `process/`: `ProcessRunner`, the only way to run an outside program.
- `recording/`: Recording sessions, the Recording bar's place, Show clicks, kept Recordings.
- `saved_colors/`: the Saved colors row.
- `settings/`: the user's `config.toml`.
- `state/`: Remembered state (`state.toml`).
- `storage/`: where files go and what they are called.
- `text/`: Text capture (Tesseract) and Text layout.
- `thumbnails/`: the Thumbnail stack, the model behind the Thumbnails.
- `timer/`: the Timer and its Countdown.
- `toml/`: TOML ↔ QVariant.
- `trim/`: the Trim service and session.
- `wallpaper/`: the wallpapers the Background panel offers.

## src/ui and src/app

- `src/ui/*_windows.cpp`: ordinary windows (Editor, Pin, Trim, History), which Hyprland can close (CODING_STANDARDS.md).
- `src/ui/*_surface.cpp`: layer surfaces (Overlay, Thumbnails, Countdown, Recording bar, ripples).
- `src/ui/qml/`: every QML file. `Style.qml` holds the design values, and `Popover.qml` is the Editor's popover base.
- `src/app/main.cpp`: the `tarn` client and `tarn daemon`.
