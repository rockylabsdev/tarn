#!/usr/bin/env python3
"""The nested bench's walk: runs INSIDE the nested Hyprland (scripts/bench-inside.sh).

It times the Overlay opening and the Thumbnail on each output, then walks every
surface (Overlay, Thumbnail, Countdown, Recording bar, Pin, Editor) on both
outputs and writes what it saw to $BENCH/walk.json, $BENCH/walk.log and
$BENCH/shots/*.png. Input is driven only through Tarn's own client commands,
the nested hyprctl and a virtual keyboard in the nested compositor.

Timings come from Hyprland's own event socket (`openlayer>>`), stamped when the
event arrives, from just before the `tarn` client starts. That is when the layer
surface is mapped; it is not proof that the first frame has been drawn.
"""
import json
import os
import socket
import struct
import subprocess
import sys
import threading
import time

BENCH = os.environ["BENCH"]
RUN = os.environ["BENCH_RUN"]
TARN = os.environ["TARN"]
SHOTS = os.path.join(BENCH, "shots")
HOME = os.environ["HOME"]
DEADLINE = time.monotonic() + float(os.environ.get("BENCH_WALK_SECONDS", "400"))

OUTPUTS = [("BENCH-1", 1, (0, 0), (1280, 720)), ("BENCH-2", 2, (1280, 0), (960, 540))]
SAMPLES = 3

results = {"timings": {}, "walk": {}, "notes": []}
# The walk is written out after every phase, so a run cut short still leaves its results.
events_ready = threading.Event()
events = []  # (perf_counter, name, data)
daemon = None
MODE = "software"  # which Qt Quick renderer the daemon runs on: "software" or "gl"


def log(*parts):
    line = " ".join(str(p) for p in parts)
    print(line, flush=True)


def guard():
    """Every call below reaches the nested compositor or nothing."""
    if os.environ.get("XDG_RUNTIME_DIR") != RUN:
        sys.exit("bench: not in the private runtime folder")
    for var in ("HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"):
        if not os.environ.get(var, "").startswith(BENCH + "/"):
            sys.exit("bench: %s is not under the scratch folder" % var)
    wl = os.environ.get("WAYLAND_DISPLAY", "")
    sock = wl if wl.startswith("/") else os.path.join(RUN, wl)
    if not wl or not os.path.dirname(sock) == RUN.rstrip("/"):
        sys.exit("bench: WAYLAND_DISPLAY is not the nested socket: %r" % wl)
    if not os.path.isdir(os.path.join(RUN, "hypr", os.environ.get("HYPRLAND_INSTANCE_SIGNATURE", "none"))):
        sys.exit("bench: no nested Hyprland instance")


def run(cmd, timeout=10, **kw):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, **kw)
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except subprocess.TimeoutExpired:
        return 124, "timeout"


def hypr_json(*args):
    rc, out = run(["hyprctl", *args, "-j"])
    try:
        return json.loads(out)
    except ValueError:
        return None


def dispatch(lua):
    out = run(["hyprctl", "dispatch", lua])[1].strip()
    if out != "ok":
        log("   dispatch", lua, "->", out)
    return out


def events_thread():
    sig = os.environ["HYPRLAND_INSTANCE_SIGNATURE"]
    s = socket.socket(socket.AF_UNIX)
    # The path is longer than a socket address allows: connect from inside the folder.
    # Hyprland may not have made the socket yet when the walk starts, so retry for a while.
    # A symlink in the scratch folder is short enough, and the kernel follows it.
    link = os.path.join(BENCH, "ev.sock")
    target = os.path.join(RUN, "hypr", sig, ".socket2.sock")
    if not os.path.islink(link):
        os.symlink(target, link)
    err = None
    for _ in range(100):
        try:
            s.connect(link)
            break
        except OSError as e:
            err = e
            time.sleep(0.1)
    else:
        log("event socket:", repr(err), "target exists:", os.path.exists(target), "dir:", os.listdir(os.path.dirname(target)))
        return
    events_ready.set()
    buf = b""
    while True:
        data = s.recv(65536)
        if not data:
            return
        now = time.perf_counter()
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            name, _, rest = line.decode(errors="replace").partition(">>")
            events.append((now, name, rest))


def wait_event(name, match, since, timeout=6.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        for ev in events[since:]:
            if ev[1] == name and match in ev[2]:
                return ev
        time.sleep(0.005)
    return None


def pause(seconds):
    time.sleep(seconds)


def layers():
    out = []
    data = hypr_json("layers") or {}
    for mon, d in data.items():
        for lvl, arr in d.get("levels", {}).items():
            for l in arr:
                out.append({"monitor": mon, "level": int(lvl), "ns": l["namespace"], "x": l["x"], "y": l["y"], "w": l["w"], "h": l["h"]})
    return out


def layer(ns):
    return [l for l in layers() if l["ns"] == ns]


def monitors():
    return {m["name"]: m for m in (hypr_json("monitors") or [])}


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    return struct.unpack(">II", head[16:24])


def grim(mon, name):
    path = os.path.join(SHOTS, MODE + "-" + name)
    rc, out = run(["grim", "-o", mon, path], timeout=15)
    return path if rc == 0 else None


def key(*names):
    for n in names:
        run(["wtype", "-k", n] if len(n) > 1 else ["wtype", n])
        pause(0.08)


def drag_keys(right, down):
    """Shift+arrows in one wtype call: each press moves the pointer and the selection by 10 px."""
    run(["wtype", "-M", "shift"] + ["-k", "Right"] * right + ["-k", "Down"] * down + ["-m", "shift"], timeout=15)


def focus_monitor(name, pos, logical, offset=(0.5, 0.5)):
    """Put the nested pointer on a monitor. Never the host's: hyprctl only reaches the nested one."""
    dispatch('hl.dsp.focus({monitor="%s"})' % name)
    x = pos[0] + int(logical[0] * offset[0])
    y = pos[1] + int(logical[1] * offset[1])
    dispatch("hl.dsp.cursor.move({x=%d, y=%d})" % (x, y))
    pause(0.3)
    cur = hypr_json("cursorpos")
    mons = monitors()
    focused = [n for n, m in mons.items() if m.get("focused")]
    return {"pointer": cur, "focused": focused}


def history_pngs():
    d = os.path.join(HOME, ".local/share/tarn/history")
    try:
        files = sorted((os.path.getmtime(os.path.join(d, f)), f) for f in os.listdir(d) if f.endswith(".png"))
    except OSError:
        return []
    return [(f, png_size(os.path.join(d, f))) for _, f in files]


# --- daemon -------------------------------------------------------------------------------

def write_config(close_after):
    d = os.path.join(HOME, ".config/tarn")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "config.toml"), "w") as f:
        f.write("close_thumbnail_after = %d\n" % close_after)


def start_daemon(tag, gl=False):
    """Qt Quick on the CPU software renderer, or (gl) the normal OpenGL path on Mesa's llvmpipe,
    which draws every shader effect on the CPU. Only the daemon is moved off the host's GPU."""
    global daemon
    env = dict(os.environ, QT_QPA_PLATFORM="wayland", QT_LOGGING_RULES="qt.qpa.wayland.*=false")
    if gl:
        env.update(QSG_RHI_BACKEND="opengl", LIBGL_ALWAYS_SOFTWARE="1", GALLIUM_DRIVER="llvmpipe",
                   __EGL_VENDOR_LIBRARY_FILENAMES="/usr/share/glvnd/egl_vendor.d/50_mesa.json")
    else:
        env["QT_QUICK_BACKEND"] = "software"
    t0 = time.perf_counter()
    daemon = subprocess.Popen([TARN, "daemon"], env=env, stdout=open(os.path.join(BENCH, "tarn-%s.log" % tag), "w"), stderr=subprocess.STDOUT)
    pause(2.5)
    if daemon.poll() is not None:
        log("daemon exited at start", daemon.returncode)
        return False
    maps = ""
    try:
        with open("/proc/%d/maps" % daemon.pid) as f:
            maps = f.read()
    except OSError:
        pass
    results["notes"].append("daemon %s: libgallium mapped=%s, nvidia egl mapped=%s" % (tag, "libgallium" in maps, "nvidia" in maps))
    return True


def save():
    with open(os.path.join(BENCH, "events.log"), "w") as f:
        t0 = events[0][0] if events else 0
        for t, name, data in events:
            f.write("%.3f %s>>%s\n" % (t - t0, name, data))
    with open(os.path.join(BENCH, "walk.json"), "w") as f:
        json.dump(results, f, indent=1, default=str)


def stop_daemon():
    global daemon
    save()
    run([TARN, "quit"], timeout=5)
    try:
        daemon.wait(timeout=4)
    except subprocess.TimeoutExpired:
        daemon.kill()
    daemon = None
    pause(0.5)


# --- timings ------------------------------------------------------------------------------

def timed(cmd, ns):
    """Run a tarn client command and wait for the layer surface `ns` to map."""
    since = len(events)
    t0 = time.perf_counter()
    rc, out = run([TARN, *cmd])
    t_cmd = time.perf_counter() - t0
    ev = wait_event("openlayer", ns, since)
    t_layer = (ev[0] - t0) if ev else None
    return {"rc": rc, "client_returned_ms": round(t_cmd * 1000, 1), "layer_mapped_ms": round(t_layer * 1000, 1) if t_layer is not None else None}, since


def timing_phase():
    results["timings"][MODE] = {}
    for name, scale, pos, logical in OUTPUTS:
        state = focus_monitor(name, pos, logical)
        t = {"scale": scale, "pointer": state, "overlay": [], "thumbnail": [], "baseline": {}}
        base = []
        for i in range(3):
            t0 = time.perf_counter()
            run(["grim", "-o", name, "/dev/null"])
            base.append(round((time.perf_counter() - t0) * 1000, 1))
        t["baseline"]["grim_ms"] = base
        base = []
        for i in range(3):
            t0 = time.perf_counter()
            run([TARN, "--version"])
            base.append(round((time.perf_counter() - t0) * 1000, 1))
        t["baseline"]["tarn_client_ms"] = base
        results["timings"][MODE][name] = t
        for i in range(SAMPLES):
            sample, since = timed(["capture", "area"], "tarn-overlay")
            seen = layer("tarn-overlay")
            sample["monitor"] = [l["monitor"] for l in seen]
            t["overlay"].append(sample)
            pause(0.5)
            key("Escape")
            wait_event("closelayer", "tarn-overlay", since)
            pause(0.4)
        for i in range(SAMPLES):
            end = time.monotonic() + 4
            while layer("tarn-thumbnail") and time.monotonic() < end:
                pause(0.2)
            sample, since = timed(["capture", "fullscreen"], "tarn-thumbnail")
            pause(0.3)
            sample["monitor"] = [l["monitor"] for l in layer("tarn-thumbnail")]
            t["thumbnail"].append(sample)
            wait_event("closelayer", "tarn-thumbnail", since, timeout=4)
            pause(0.4)
        log("timing", name, json.dumps(t))


# --- the walk -----------------------------------------------------------------------------

def window_info(addr):
    mons = {m["id"]: m["name"] for m in (hypr_json("monitors") or [])}
    for c in hypr_json("clients") or []:
        if c["address"] == "0x" + addr:
            return {"title": c["title"], "at": c["at"], "size": c["size"], "monitor": mons.get(c["monitor"]), "floating": c["floating"], "pinned": c.get("pinned")}
    return None


def close_window(addr):
    dispatch('hl.dsp.window.close({window="address:0x%s"})' % addr)
    pause(0.4)


def open_window_by_keys(keys, title_part, wait=6.0):
    since = len(events)
    key(*keys)
    ev = wait_event("openwindow", title_part, since, wait)
    if not ev:
        return None, None
    addr = ev[2].split(",")[0]
    pause(0.8)
    return addr, window_info(addr)


def reset():
    """Leave nothing of the last step open: an Overlay left up takes every key and refuses the next command."""
    for _ in range(3):
        if layer("tarn-overlay"):
            key("Escape")
            pause(0.5)
    for c in hypr_json("clients") or []:
        if c["title"].startswith("Tarn "):
            dispatch('hl.dsp.window.close({window="address:%s"})' % c["address"])
    pause(0.5)


def step(rec, name, fn, pre=None):
    try:
        if pre:
            pre()
        rec[name] = fn()
    except Exception as e:  # keep walking; the log shows what broke
        rec[name] = {"error": repr(e)}
    log("  ", name, json.dumps(rec[name], default=str))


def walk_output(name, scale, pos, logical, full=True):
    rec = {"scale": scale}
    results["walk"].setdefault(MODE, {})[name] = rec
    mons = monitors()
    rec["monitor"] = {k: mons[name].get(k) for k in ("x", "y", "width", "height", "scale", "reserved")} if name in mons else None
    rec["pointer"] = focus_monitor(name, pos, logical)
    log("walk", name, json.dumps(rec["monitor"]))
    tag = name.lower()

    def overlay_view():
        since = len(events)
        out = timed(["capture", "area"], "tarn-overlay")[0]
        pause(0.6)
        out["layers"] = layer("tarn-overlay")
        out["shot"] = grim(name, "overlay-%s.png" % tag)
        key("Escape")
        wait_event("closelayer", "tarn-overlay", since)
        pause(0.4)
        return out

    def area_capture():
        """Keyboard drag: Enter starts at the pointer, Shift+arrows grow it 300 x 200, Enter confirms."""
        before = len(history_pngs())
        since = len(events)
        timed(["capture", "area"], "tarn-overlay")
        pause(0.4)
        key("Return")
        pause(0.2)
        drag_keys(30, 20)
        pause(0.6)
        mid = grim(name, "area-drag-%s.png" % tag)
        key("Return")
        wait_event("openlayer", "tarn-thumbnail", since, 4)
        pause(1.2)
        pngs = history_pngs()
        return {"history_png_sizes": pngs[before:], "expected_px": [300 * scale, 200 * scale], "drag_shot": mid,
                "thumbnail_layers": layer("tarn-thumbnail"), "thumbnail_shot": grim(name, "thumb-area-%s.png" % tag)}

    def overlay_full():
        """F in the Overlay: the whole monitor, cropped from the frozen shot (not `capture fullscreen`)."""
        before = len(history_pngs())
        since = len(events)
        timed(["capture", "area"], "tarn-overlay")
        pause(0.4)
        key("f")
        wait_event("closelayer", "tarn-overlay", since, 4)
        pause(1.2)
        return {"history_png_sizes": history_pngs()[before:], "expected_px": [logical[0] * scale, logical[1] * scale],
                "thumbnail_layers": layer("tarn-thumbnail")}

    def fullscreen_capture():
        before = len(history_pngs())
        out = timed(["capture", "fullscreen"], "tarn-thumbnail")[0]
        pause(1.0)
        pngs = history_pngs()
        out["history_png_sizes"] = pngs[before:]
        out["expected_px"] = [logical[0] * scale, logical[1] * scale]
        out["thumbnail_layers"] = layer("tarn-thumbnail")
        out["shot"] = grim(name, "thumb-full-%s.png" % tag)
        return out

    def countdown(label):
        m = monitors()[name]
        reserved = m.get("reserved")
        since = len(events)
        run([TARN, "capture", "timer"])
        wait_event("openlayer", "tarn-overlay", since)
        pause(0.4)
        key("f")
        ev = wait_event("openlayer", "tarn-countdown", since, 4)
        pause(0.6)
        out = {"reserved_before": reserved, "expected_top": (reserved[1] if reserved else 0) + 12,
               "layers": layer("tarn-countdown"), "shot": grim(name, "countdown-%s-%s.png" % (label, tag))}
        wait_event("closelayer", "tarn-countdown", since, 6)
        pause(1.2)
        return out

    def record():
        gsr_before = 0
        try:
            gsr_before = len(open(os.path.join(BENCH, "gsr.log")).read().splitlines())
        except OSError:
            pass
        since = len(events)
        run([TARN, "capture", "record"])
        wait_event("openlayer", "tarn-overlay", since)
        pause(0.4)
        key("Return")
        pause(0.2)
        drag_keys(30, 20)
        pause(0.4)
        key("Return")  # keeps the area for the Record panel
        pause(0.5)
        panel = grim(name, "record-panel-%s.png" % tag)
        key("Return")  # Record
        ev = wait_event("openlayer", "tarn-recording", since, 5)
        pause(1.5)
        out = {"recording_layers": layer("tarn-recording"), "panel_shot": panel, "bar_shot": grim(name, "recording-%s.png" % tag),
               "monitors_after": {k: v.get("reserved") for k, v in monitors().items()}}
        try:
            out["gsr_args"] = open(os.path.join(BENCH, "gsr.log")).read().splitlines()[gsr_before:]
        except OSError:
            out["gsr_args"] = []
        run([TARN, "capture", "record"])  # stops it
        wait_event("closelayer", "tarn-recording", since, 5)
        pause(1.5)
        return out

    def editor_and_pin():
        out = {}
        since = len(events)
        run([TARN, "history"])
        ev = wait_event("openwindow", "Tarn history", since, 6)
        pause(1.0)
        if not ev:
            return {"error": "History window did not open"}
        haddr = ev[2].split(",")[0]
        out["history"] = window_info(haddr)
        out["history_shot"] = grim(name, "history-%s.png" % tag)
        key("Right")  # the window opens with nothing selected
        eaddr, einfo = open_window_by_keys(["Return"], "Tarn editor")
        out["editor"] = einfo
        out["editor_shot"] = grim(name, "editor-%s.png" % tag)
        if eaddr:
            close_window(eaddr)
        pause(0.6)
        paddr, pinfo = open_window_by_keys(["p"], "Tarn pin")
        out["pin"] = pinfo
        out["pin_shot"] = grim(name, "pin-%s.png" % tag)
        if paddr:
            close_window(paddr)
        close_window(haddr)
        return out

    def fresh():
        reset()
        focus_monitor(name, pos, logical)

    if full:
        step(rec, "overlay", overlay_view, fresh)
        step(rec, "area_capture", area_capture, fresh)
        step(rec, "overlay_full", overlay_full, fresh)
    step(rec, "fullscreen_capture", fullscreen_capture, fresh)
    step(rec, "countdown_with_reserved", lambda: countdown("reserved"), fresh)
    if full and name == "BENCH-1":
        def clear_reserved():
            dispatch('hl.dsp.cursor.move({x=%d, y=%d})' % (pos[0] + 10, pos[1] + 10))
            run(["hyprctl", "eval", 'hl.monitor({output="BENCH-1", mode="1280x720@60", position="0x0", scale=1, reserved={top=0, right=0, bottom=0, left=0}})'])
            pause(1.2)
            focus_monitor(name, pos, logical)
            return countdown("noreserved")
        step(rec, "countdown_no_reserved", clear_reserved, fresh)
    step(rec, "record", record, fresh)
    step(rec, "editor_and_pin", editor_and_pin, fresh)


def doctor():
    rc, out = run([TARN, "doctor"], timeout=20)
    results["doctor"] = {"exit": rc, "output": out}
    log("doctor exit", rc)
    log(out)


def main():
    guard()
    os.makedirs(SHOTS, exist_ok=True)
    threading.Thread(target=events_thread, daemon=True).start()
    if not events_ready.wait(15):  # no timings without the event socket: end the run at once
        sys.exit("bench: could not reach Hyprland's event socket")

    global MODE
    log("== timing phase, CPU software renderer (Thumbnail closes after 1 s)")
    write_config(1)
    if start_daemon("timing-software"):
        timing_phase()
    stop_daemon()

    log("== timing phase, OpenGL on llvmpipe")
    MODE = "gl"
    if start_daemon("timing-gl", gl=True):
        timing_phase()
    stop_daemon()

    log("== walk phase, CPU software renderer")
    MODE = "software"
    write_config(0)
    if start_daemon("walk-software"):
        for out in OUTPUTS:
            if time.monotonic() < DEADLINE:
                walk_output(*out)
        doctor()
    stop_daemon()

    log("== walk phase, OpenGL on llvmpipe (what the cards and bars look like with shaders)")
    MODE = "gl"
    if start_daemon("walk-gl", gl=True):
        for out in OUTPUTS:
            if time.monotonic() < DEADLINE:
                walk_output(*out, full=False)
    stop_daemon()

    with open(os.path.join(BENCH, "walk.json"), "w") as f:
        json.dump(results, f, indent=1, default=str)
    log("done")


main()
