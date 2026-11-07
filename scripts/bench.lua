-- The nested bench's Hyprland config (scripts/bench.sh). Lua is the only format.
-- It loads the real hypr/tarn.lua, so the bench runs the binds and rules Tarn ships.
-- Only the autostart is switched off: the walk starts the daemon itself, with Qt Quick
-- on the CPU renderer and the stub programs first on PATH.
-- tarn.lua looks `hl.exec_cmd` up when Hyprland starts, so the wrapper stays in place.
local exec_cmd = hl.exec_cmd
hl.exec_cmd = function (cmd, ...)
    if cmd == "tarn daemon" then
        return
    end
    return exec_cmd(cmd, ...)
end
dofile(os.getenv("BENCH") .. "/tarn.lua")

hl.on("hyprland.start", function ()
    hl.exec_cmd("sh " .. os.getenv("BENCH") .. "/inside.sh > " .. os.getenv("BENCH") .. "/inside.log 2>&1")
end)
