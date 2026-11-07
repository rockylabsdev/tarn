# The Frame

Read when working on the Frame (Background, Padding, Shadow, Corners, Ratio) or its panel.

- `src/core/frame`: `Frame` (a value: Background, Padding, Inset, Auto-balance, Shadow, Corners, Alignment, Ratio; fractions of the shot's shorter side), `layoutFrame(contentSize, frame)` and `paintFrameBehind`. `CaptureDocument` owns one (`frame()`, `setFrame()` records one undo step; `setFrameLive` + `recordFrameChange` for a drag). A Background kind is one file with a `RegisterBackgroundKind` (copy `plain_background.cpp`); "none" is not a kind, it means no Frame.
- `EditorSession`: `frame` (map), `setFrameBackground(kind, params)`, `setFrameValue(key, value)`, `dragFrameValue` / `dragFrameBackground` + `endFrameDrag`. Annotation coordinates stay in the content's own pixels; `viewToDoc` / `docToView` add the Frame's offset.
- `tests/frame` compares reference PNGs (`tests/frame/reference/<control>-small|large.png`, shots 96x64 and 288x192) exactly; regenerate as verify.md section 2 says. `tests/frame_ui` drives the panel in the real Editor window off-screen.
