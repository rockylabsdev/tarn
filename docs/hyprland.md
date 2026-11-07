# Hyprland and Wayland facts Tarn depends on

Measured on the target machine (RTX 4090, one 2560x1440 monitor) during the v1 prototypes and build, Hyprland 0.56, re-checked against 0.57. The code shows *what* Tarn does; this file holds *why*, and the traps nothing in the code confesses. Everything Tarn asks of Hyprland goes through `HyprlandBridge` (`src/core/hyprland/`).

## The user's config is Lua

- **Lua is the only config format since Hyprland 0.57**, which removed `hyprland.conf` (hyprlang; deprecated since 0.55). Tarn ships `tarn.lua` and no `.conf`.
- `~/.config/hypr/hyprland.lua` plus `config/*.lua`. Tarn's binds and rules live in `hypr/tarn.lua`, symlinked into `config/` and `require`d last (its header says how).
- **Names:** Hyprland ships its Lua API as type definitions in `/usr/share/hypr/stubs/hl.meta.lua`. Check a function or rule field there before using it: it is the version actually installed.
- **Runtime changes:** `hyprctl eval '<lua>'` only, for example `hl.dispatch(hl.dsp.cursor.move({x=1200,y=800}))`. The pre-Lua `hyprctl dispatch movecursor X Y` no longer parses and fails silently on the desktop. The test stub (`tests/support/stubs/hyprctl`) rejects it, and `keyword`, so a test fails instead.
- An `eval` lasts until the config reloads. Lua cannot delete a window rule, so Pins reuse four rule names (`tarn-pin-slot-0` to `-3`) rather than adding one per Pin.

## Queries (`hyprctl … -j`)

| Query | Read for | Fields |
|---|---|---|
| `clients` | windows the pointer can pick | `mapped`, `hidden`, `workspace.id`, `at`, `size`, `fullscreen`, `floating`, `class`, `title`, `focusHistoryID` |
| `monitors` | the monitor under the pointer; the space bars reserve | `focused`, `x`, `y`, `width`, `height`, `activeWorkspace.id`, `specialWorkspace.id` (0 = none), `reserved` |
| `binds` | whether Tarn's file is loaded | `key`, `modmask` (SUPER = 64), `non_consuming`, `description`, `dispatcher`, `arg` (see Binds) |
| `cursorpos` | pointer at open | `x`, `y`, global: subtract the monitor's `x`, `y` |
| `getoption decoration:rounding` | window corner mask | `.int` |
| `getoption cursor:no_hardware_cursors` | pointer in captures | `.int` (0 hardware, 1 software, 2 auto) |

- **`reserved` is four integers in the order left, top, right, bottom** (not CSS's top, right, bottom, left). Read on the nested bench with three layer-shell bars (top 30, bottom 20, left 10) giving `[10, 30, 0, 20]`; `hl.monitor{reserved = {top=11, right=22, bottom=33, left=44}}` reads back `[44, 11, 22, 33]`. The top is index 1. A monitor with no bar reports `[0, 0, 0, 0]`. The Countdown reads the top of the monitor it appears on when it starts and sits 12 px below it (a missing value counts as 0).
- Hyprland gives no real z-order. Window-pick order: special workspace first, then fullscreen, floating, tiled; within each, `focusHistoryID` ascending. Only `mapped && !hidden` windows on the shown workspace count.
- Each `hyprctl` call is a few milliseconds; the capture path runs several in parallel with `grim`.

## Rules

- **A rule pattern must match the whole name.** `^tarn` alone matches nothing, silently, and Hyprland then fades Tarn's surfaces over ~250 ms. Every Tarn layer namespace starts `tarn` so one rule covers them: `match = { namespace = "^tarn(-.+)?$" }, no_anim = true`. The test stub rejects an `eval` pattern without `^…$`.
- The `no_anim` layer rule is how keyboard-opened UI appears instantly: no compositor fade.
- The Editor and Trim windows need `float = true` (else Hyprland tiles them) and `center = true` (centers in the space a top bar leaves free).
- **A window's opening size must be right when it first maps.** Hyprland ignores a client's own resize once the window is on screen; later resizes only go through `hyprctl`.

## Binds

- To take over a key, `hl.unbind("<key>")` first, with the resolved string exactly as the user's binds write it (`mainMod .. " + X"` is `"SUPER + X"`).
- **How a Lua bind shows in `hyprctl binds -j`** (read on the nested bench): one object per bind with `key` (`"F9"`, or `"mouse:272"`), `modmask` (the modifiers summed: SUPER is 64, none is 0), `non_consuming`, `release`, `repeat`, `locked` and `submap`. For `hl.dsp.exec_cmd(...)` the `dispatcher` is the string `"HL.Dispatcher(exec_cmd)"` and `arg` is an opaque number in a string (`"6"`), **never the command line**. So a check cannot find Tarn's binds by looking for `tarn` in `arg`, and there is no `exec` dispatcher as in the pre-Lua shape. A mouse bind reports `mouse: false`.
- **A bind's `description` is reported as written** (read on the nested bench: `hl.bind(keys, dispatcher, { description = "..." })`, and `desc` is an alias; `has_description` is `true`, and it is `""` and `false` without one). It survives the user changing the key. Every bind in `hypr/tarn.lua` is described `Tarn: …`, and `tarn doctor` takes a bind with that prefix as proof the file is loaded. What this cannot see: the file's window rules and autostart (Hyprland lists neither for Lua config), and a copy whose descriptions were deleted.
- Autostart: `hl.on("hyprland.start", function() hl.exec_cmd("tarn daemon") end)`.
- Show clicks binds `mouse:27x` with `{non_consuming = true}` at Recording start and unbinds at stop, so clicks still reach apps.

## The pointer

- The user's `inputs.lua` sets `cursor = { no_hardware_cursors = 0 }`: a hardware pointer. `grim` and `gpu-screen-recorder` then leave it out, and the recorder's `-cursor yes` draws it in the right place.
- With a software pointer (1, or 2 = auto, which this NVIDIA card resolves to software) the pointer is baked into every capture and cannot be removed. Switching to software only takes effect at the next cursor-shape change, so Tarn cannot toggle it for one capture.
- **Qt calls Hyprland's pointer a touchpad, even a mouse.** Wayland doesn't say what kind of device a seat's pointer is, and Qt reports it as `TouchPad`. A QML `WheelHandler` (and other pointer handlers) accepts only `PointerDevice.Mouse` by default, so it silently declined every wheel event: the Editor's scroll and Ctrl+scroll did nothing on the desktop while the off-screen tests (which send a mouse) passed. Give pointer handlers `acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad`; a test that sends input should use a touchpad device too (`tarn::test::sendWheel` in `tests/support/hyprland_input.h`; the check enforces both). `MouseArea` doesn't filter by device.

## Layer surfaces (LayerShellQt)

- CMake target `LayerShellQt::Interface`; header `<LayerShellQt/window.h>`. The layer surface's scope is the namespace Hyprland's rules match.
- The Overlay: layer `Overlay`, anchored on all four edges, exclusive zone `-1` (covers any bar's strip), keyboard `Exclusive`. Nothing draws above an Overlay-layer surface except a software pointer.
- **Warm-up:** the first open of a cold layer surface takes 120–160 ms (GPU and scene-graph start-up), a warm one ~8 ms. The daemon maps the Overlay once at start, invisible and click-through, then hides it (`OverlaySurface::warmUp`).
- After hiding a surface, `grim` within ~80 ms can still catch it on screen (prototype measurement). Leave that gap before a capture that follows a hide.
- **Every full-monitor surface covers its monitor in logical pixels** (Overlay, Countdown, Recording bar: `0,0 1280x720` at 1x, `1280,0 960x540` for a 1920x1080 output at 2x), whatever a bar reserves. The Thumbnail column is the exception: its exclusive zone is 0, so Hyprland keeps it inside the reserved edges (left 44 and top 11 on a monitor that reserves them).
- **A layer surface has one output, chosen when it is created.** The Thumbnail surface is one surface for the whole stack, so it follows the monitor of the newest capture: it is told its screen before every show (a hidden window falls back to the first screen), and taken down and shown again when the newest capture is on another monitor. Left alone it stayed on the first monitor.
- A full stack is 652 logical px tall (four cards). On a monitor with less free height than that (1080p at 2x is 540 px, minus a bar) the top card is off the screen.

## Scales and small monitors (read on the nested bench)

- A capture is saved in the monitor's own pixels: a 1920x1080 output at scale 2 gives a 1920x1080 PNG, and a Selection of 300x200 logical px gives 600x400. A shot with no place on the desktop (a History entry) is shown at 1:1, so its logical size is its pixels over the scale of the monitor it opens on: pinned at its pixel size it was twice as big as the monitor, and off the screen.
- **A window's opening size must fit its monitor.** The History window is sized per open (1200x780, but no more than the monitor under the pointer less 80 px): the window is reused, and the pointer may now be on a smaller monitor. The Editor is never narrower than 1180 logical px (what its toolbar needs), so on a monitor narrower than that (1920 px wide at 2x is 960) it is wider than the screen and its left side is cut off.
- The recorder gets `-region WxH+X+Y` in the monitor's pixels (`600x400+3520+540` for the 2x output above: the global logical position times that monitor's scale). Whether the recorder reads that the same way on a desktop of mixed scales could not be tried: it cannot see a nested output.

## The nested bench: traps (`scripts/bench.sh`)

- **The host session sets `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME` and `XDG_STATE_HOME`, and they beat `HOME`.** A bench with only `HOME` changed ran the daemon on the user's real config, History, kept Recordings and remembered state. The bench sets all four to the scratch folder, and its scripts refuse to run when any is outside it.
- **Keep the scratch path short.** Hyprland puts its event socket at `<runtime>/hypr/<signature>/.socket2.sock`; the signature is about 62 characters and a path over 107 bytes silently turns the event socket off (`hyprland.log`: "path is too long. IPC will not work"), but not `hyprctl`. The folder name's length decides whether it works, and the signature's last number varies in length.
- The event socket is the bench's clock: `openlayer>>tarn-overlay`, `closelayer>>…`, `openwindow>>addr,workspace,class,title` arrive with the moment a surface maps, which is more exact than polling `hyprctl layers`. Each `grim` shows as a pair of `screencast>>` events: ignore them.
- Keys go through `wtype` (a virtual keyboard inside the nested compositor): `Escape` and letters reach an Overlay with exclusive keyboard, and Shift+arrows in one call grow a keyboard drag by 10 px a press. `hyprctl dispatch 'hl.dsp.cursor.move({x=…, y=…})'` moves the nested pointer; there is no click dispatcher. A window opened by the History window needs a selection first (it opens with none): Right, then Return or P.
- An Overlay left open takes every key and makes the next `tarn capture …` return at once without opening one. Close it (Escape) between steps.
- Qt Quick on `QT_QUICK_BACKEND=software` draws the Thumbnail cards as empty outlines (their rounded clipping needs shaders). The same daemon on the OpenGL path with Mesa's llvmpipe (`QSG_RHI_BACKEND=opengl LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json`) draws everything on the CPU and moves only the daemon off the host's GPU; the bench runs both.
- Layout and open times only. gpu-screen-recorder stays a stub (`scripts/bench-stubs/`), and the compositor still draws on the host's GPU.
