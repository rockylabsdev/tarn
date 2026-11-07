# Show clicks

Read when working on click ripples in a Recording.

- `ClickRipples` service (`src/core/recording/click_ripples.*`, command `record click <left|middle|right>` = the internal click notice the binds run). Tests (`tests/click_ripples`) read the stubbed `hyprctl eval <lua>` calls: one `hl.bind("mouse:27x", hl.dsp.exec_cmd("tarn record click ..."), {non_consuming=true})` line set at start, one `hl.unbind("mouse:27x")` set on stop, delete, a dying recorder, a toggle turned off before a restart, or (crash) at the next daemon start (`[record] clicks_bound` in state.toml). The pointer comes from the stub's `cursorpos.json` (`env.setPointer`).
- `RippleSurface` (`src/ui/ripple_surface.*`, `Ripples.qml`, scope `tarn-ripples`) is tested off-screen; whether it is click-through and shows up in a real region Recording is needs-eyes.
