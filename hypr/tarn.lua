-- Tarn for Hyprland: https://github.com/rockylabsdev/tarn
--
-- What this is: Hyprland's settings for Tarn. It starts Tarn's daemon with
-- Hyprland, sets how Tarn's windows behave, and binds the Print key family.
-- (Hyprland reads Lua files for its configuration; "--" starts a comment.)
--
-- Where it goes: copy this file to ~/.config/hypr/config/tarn.lua and add this
-- line at the end of ~/.config/hypr/hyprland.lua:
--     require("config.tarn")
-- then run `hyprctl reload`. `tarn doctor` checks that it is loaded.
--
-- Every bind below has a description starting "Tarn:". `tarn doctor` finds this file
-- in `hyprctl binds` by it (Hyprland does not report a Lua bind's command), and it
-- still finds the file when you change a key. Keep the prefix on any bind you add.
--
-- It only adds binds and rules; it never unbinds, and it never removes yours. If
-- a key below already does something in your config, remove one of the two binds:
-- Hyprland runs both.
--
-- Tarn also needs hardware cursors to keep the pointer out of screenshots. If you
-- turned them off, add this to your own config:
--     hl.config({ cursor = { no_hardware_cursors = 0 } })

-- Start the daemon with Hyprland. It runs for the whole session and owns every
-- Tarn surface, so they open instantly.
hl.on("hyprland.start", function ()
    hl.exec_cmd("tarn daemon")
end)

-- Tarn's surfaces open with no animation (a fade on a shortcut-opened popup
-- felt less snappy). The pattern must match the WHOLE namespace: "^tarn(-.+)?$"
-- covers tarn-thumbnail, tarn-overlay, ...; "^tarn" silently matches nothing and
-- Hyprland then fades every Tarn surface in over about 250 ms.
hl.layer_rule({
    name = "tarn",
    match = { namespace = "^tarn(-.+)?$" },
    no_anim = true,
})

-- The Editor is an ordinary window (title "Tarn editor"). Without these rules
-- Hyprland would tile it; with them it floats, opens centered and with no
-- animation. Its size is not set here: Tarn computes it from the monitor and the
-- shot and sets it before the first show (Hyprland ignores a window's later
-- resizes). Dragging an empty part of a bar moves it.
hl.window_rule({
    name = "tarn-editor",
    match = { title = "^(Tarn editor.*)$" },
    float = true,
    center = true,
    no_anim = true,
})

-- A Pin is an ordinary window titled "Tarn pin <number>": a capture floated above
-- everything. `float` makes it a floating window, and `pin` is what keeps it on
-- every workspace AND above every other window: Hyprland draws a pinned floating
-- window in a pass after all the others ("pinned always above"). Pinning is
-- ignored for a tiled window, which is why `float` is here too. (A layer surface
-- would also sit above, but it cannot be dragged by the compositor and gets no
-- keyboard focus for Esc and Ctrl+C.) `no_initial_focus` stops a new Pin taking
-- the keyboard from whatever the user is typing in; click it, then Esc closes it.
-- Where a Pin opens (its captured position and size) is not here: Tarn sets it with a rule
-- added through `hyprctl eval` as it opens each Pin. Hyprland's Lua cannot delete a rule, so
-- Tarn reuses four rule names (tarn-pin-slot-0..3) instead of adding one per Pin.
-- Known Hyprland behavior: a fullscreen window covers a Pin, because
-- `allow_pin_fullscreen` is off by default.
hl.window_rule({
    name = "tarn-pin",
    match = { title = "^(Tarn pin [0-9]+)$" },
    float = true,
    pin = true,
    no_anim = true,
    no_initial_focus = true,
    keep_aspect_ratio = true, -- a mouse resize keeps the picture's shape; Tarn snaps back if one slips through
    no_blur = true, -- at lowered opacity (Ctrl+scroll) what's behind must stay readable, not blurred
})

-- SUPER + SHIFT + P: lock every Pin (clicks pass through it, it never takes focus),
-- or unlock them all when every Pin is already locked. A Locked Pin can't be focused,
-- so this is its only way back.
hl.bind("SUPER + SHIFT + P", hl.dsp.exec_cmd("tarn pin lock-toggle"), { description = "Tarn: lock or unlock Pins" })

-- The Trim window (title "Tarn trim") is the same: floated, centered, no animation,
-- sized by Tarn before its first show, moved by dragging an empty part of a bar.
hl.window_rule({
    name = "tarn-trim",
    match = { title = "^(Tarn trim.*)$" },
    float = true,
    center = true,
    no_anim = true,
})

-- Print: the Overlay on a frozen image of the monitor under the pointer, on
-- Screenshot, with the Mode bar at the top (1 Screenshot, 2 Record, 3 Text, 4 Color; T Timer;
-- F full screen). Drag an area and let go to capture it; Esc or a right-click cancels.
-- The other keys below open the same Overlay preset to a Mode.
hl.bind("Print", hl.dsp.exec_cmd("tarn capture area"), { description = "Tarn: capture an area" })

-- SHIFT + Print: the focused app's last area again, with no Overlay. When that app
-- has none yet it opens the Overlay like Print.
hl.bind("SHIFT + Print", hl.dsp.exec_cmd("tarn capture repeat"), { description = "Tarn: repeat the last area" })

-- SUPER + SHIFT + Print: the Overlay on Screenshot with the Timer on. Drag an area (or press F for the full screen);
-- a Countdown runs in a corner of it, then the shot is taken. Pressed again during
-- the Countdown, it cancels.
hl.bind("SUPER + SHIFT + Print", hl.dsp.exec_cmd("tarn capture timer"), { description = "Tarn: capture with a Timer" })

-- CTRL + Print: the Overlay on Record (area only). Pick an area, press Enter
-- (or the Record Video button) to start an MP4; CTRL + Print again stops it. Esc
-- never stops a Recording.
hl.bind("CTRL + Print", hl.dsp.exec_cmd("tarn capture record"), { description = "Tarn: record an area" })

-- SUPER + Print: full-screen shot of the monitor under the pointer, shown as a
-- Thumbnail in the bottom-left corner.
hl.bind("SUPER + Print", hl.dsp.exec_cmd("tarn capture fullscreen"), { description = "Tarn: capture the full screen" })

-- SUPER + CTRL + Print: bring back the last closed Thumbnail (whatever closed
-- it), with its edits.
hl.bind("SUPER + CTRL + Print", hl.dsp.exec_cmd("tarn restore"), { description = "Tarn: restore the last Thumbnail" })

-- CTRL + SHIFT + Print: Text capture. The Overlay on Text (drag an area, or click a
-- window); the text in the area is copied and one notification shows
-- its first line, or "No text found". Never added to History.
hl.bind("CTRL + SHIFT + Print", hl.dsp.exec_cmd("tarn capture text"), { description = "Tarn: Text capture" })

-- ALT + Print: the History window, a grid of every kept capture; again (while it is
-- focused) closes it. Bound by KEY CODE: holding Alt changes the Print key's symbol
-- to Sys_Req, so a bind on "ALT + Print" never fires. 107 is the Print key's xkb
-- key code (evdev 99 + 8); check yours with `wev`. The kernel's magic SysRq only acts on a third key held with
-- Alt+SysRq, and kernel.sysrq = 16 allows only "sync", so this reaches Hyprland.
hl.bind("ALT + code:107", hl.dsp.exec_cmd("tarn history"), { description = "Tarn: History window" })

-- The History window (title "Tarn history") is an ordinary window like the
-- Editor: floated, centered, no animation. Tarn sets its size before the first show.
hl.window_rule({
    name = "tarn-history",
    match = { title = "^(Tarn history)$" },
    float = true,
    center = true,
    no_anim = true,
})
