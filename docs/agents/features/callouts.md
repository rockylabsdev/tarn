# Callouts

Read when working on Callouts or the Counter tool's gestures.

- `CalloutAnnotation` (`src/core/annotations/callout.*`) stores its Counter's id, the box's top-left and the text; color, size and the Counter's place are handed over by the document (`Annotation::anchorTo`, called whenever the document is read), so nothing is stale when a Counter moves, recolors or renumbers. `CaptureDocument::remove` deletes the Callouts of removed Counters. It is saved by `toJson()` and a loader, like any other kind, and has no tool of its own.
- The Counter tool's gestures live in `EditorSession` (`editor_session_callout.cpp`, `Mode::Pulling`); the box text is typed through the Text path (`beginText`-style: `setText`, `finishText`, `editText`, `editSelectedCallout` for Enter).
- `tests/callout` drives all of it through `EditorSession` (`pointerPressed/Moved/Released`, `setText`, `finishText`) and compares reference PNGs (`tests/callout/reference/`, regenerate as verify.md section 2 says). Leader geometry is also checked numerically against the box edge.
