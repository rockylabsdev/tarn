# The clipboard

Read when working on Copy, or anything that puts a capture or Recording on the clipboard.

Clipboard paste checks (Copy, later Recordings) are always needs-eyes. Copy runs the helper `tarn-clip` (built next to `tarn`, protocol code in `src/core/clipboard/helper/`), which talks to the real compositor. Tests only assert how the daemon launches it (the stub) and what it would offer (`clip_offers`). Never run the real `tarn-clip` against the user's session; with no `WAYLAND_DISPLAY` it just exits 1.
