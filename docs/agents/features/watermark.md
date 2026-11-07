# The Watermark

Read when working on the Watermark.

- `src/core/frame/watermark.*`: `Watermark` is a field of `Frame` (`frame.watermark`), so Presets, B, undo and the Project file treat it as part of the Frame. `paintWatermark` is called last by `CaptureDocument::paintFinished` (with or without a Background), clipped to the shot's corners. `Watermark::image` is the loaded picture; the Presets file keeps only `path` (`[preset."Name".watermark]`) and `PresetStore::resolve` reads the file each time a Preset is applied (an unreadable one switches the Watermark off, with one notification when `notify` is true: the auto-applied Preset and `applyPreset`). The Project file embeds the picture as base64 PNG in `frame.watermark.png`.
- `EditorSession`: `setFrameValue` / `dragFrameValue` keys `wmMode`, `wmText`, `wmCorner` (0 TL, 1 TR, 2 BL, 3 BR), `wmSize`, `wmOpacity`; `setWatermarkImage(path)` for the chooser.
- `tests/watermark` compares reference PNGs (`tests/watermark/reference`, regenerate as verify.md section 2 says); text references need the bundled Geist, so that test calls `tarn::loadFonts()`. The panel section is covered in `tests/frame_ui`.
