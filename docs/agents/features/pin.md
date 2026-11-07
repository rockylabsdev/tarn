# Pin

Read when working on Pins, their Hyprland rules, Locked Pins.

- `Pins` service (`src/core/pin`, `daemon.service<Pins>()`): `pin(thumbnailId)`, `resize/changeOpacity(pin, angleDeltaY)`, `copy`, `saveAs`, `edit`, `close`, `closeAll`. Tests (`tests/pin`) read what Hyprland was asked from the stub: `hyprctl eval <lua>` calls (the opening rule, then the resize and move). Put a Pin's window in the `clients.json` fixture (`{"title":"Tarn pin 1","at":[x,y],"size":[w,h]}`) to model the user having dragged it.
- **Locked:** `Pins::lock/unlock/lockToggle` (command `pin lock-toggle`) ask the bridge for `hyprctl eval` with `hl.dispatch(hl.dsp.window.set_prop({window="address:<addr>", prop="no_focus", value="1"|"0"}))` (checked on Hyprland 0.57.1; the old `dispatch setprop` form is refused); the address comes from the `clients.json` fixture (`{"address":"0xa1","title":"Tarn pin 1",...}`). Tests read those `eval` calls from the stub's log. That Hyprland really passes clicks through is needs-eyes.
- `tests/pin_ui` drives the real Pin window and the Thumbnail's Pin button off-screen. Prefer running test binaries through `ctest`; every test binary also forces the off-screen platform itself (`tests/support/force_offscreen.cpp`).
