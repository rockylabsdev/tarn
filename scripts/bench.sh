#!/bin/sh
# The nested bench: a Hyprland inside the host's Wayland session, with a 1x and a 2x
# headless output, the Tarn daemon started from its `hyprland.start` hook with Qt
# Quick on the CPU software renderer. It shows real Hyprland behavior (layer
# surfaces, scales, `hyprctl` answers, grim) without touching the host's compositor.
#
# usage: scripts/bench.sh [seconds-to-stay-up, default 480]
# It runs the walk in scripts/bench-walk.py: timings of the Overlay and the Thumbnail
# on each output, then every surface on both. The run ends when the walk does (about
# two and a half minutes); the number is only the backstop.
#
# One window appears on the host desktop for the length of the run: Hyprland cannot
# start with only the headless backend (no seat, no host), so it needs a host
# compositor to be a window in. Nothing here records: gpu-screen-recorder lists the
# real DRM connectors, never a nested output.
#
# Isolation, each line closes a way to reach the host (docs/agents/verify.md):
#   - dbus-run-session: a private session bus, so notify-send and portal requests
#     (gpu-screen-recorder's probes start a ScreenCast session) never reach the host's;
#   - XDG_RUNTIME_DIR is a private 0700 folder: the nested wayland socket, Hyprland's
#     hypr/<signature> folder and Tarn's socket stay out of the host's;
#   - HOME and all four XDG_*_HOME folders are scratch. The host session sets XDG_CONFIG_HOME,
#     XDG_DATA_HOME, XDG_CACHE_HOME and XDG_STATE_HOME, and those beat HOME: with HOME alone the
#     daemon read the user's config.toml and wrote History, kept Recordings and state.toml
#     into the real folders;
#   - DISPLAY and the host's Hyprland variables are removed;
#   - the host display is passed as an absolute path (libwayland accepts it);
#   - the inside script runs hyprctl only after checking it is in the private folder.
# AQ_BACKENDS is not an Aquamarine variable: it does not select a headless-only run.
set -eu

here=$(dirname "$0")
root=$(cd "$here/.." && pwd)

host_wl=${WAYLAND_DISPLAY:-wayland-1}
case $host_wl in
    /*) ;;
    *) host_wl="${XDG_RUNTIME_DIR:?needs the host session}/$host_wl" ;;
esac
[ -S "$host_wl" ] || { echo "bench: no host Wayland socket at $host_wl" >&2; exit 2; }

tarn=${TARN:-$root/build/src/app/tarn}
[ -x "$tarn" ] || { echo "bench: no tarn binary at $tarn (run scripts/check.sh first)" >&2; exit 2; }

# A short path on purpose: Hyprland puts its event socket at <runtime>/hypr/<signature>/.socket2.sock,
# the signature is about 62 characters, and a path over 107 bytes silently turns the event socket off.
scratch=$(mktemp -d "${TMPDIR:-/tmp}/tb-XXXXXX")
mkdir -m 700 "$scratch/r" "$scratch/home"
cp "$here/bench-inside.sh" "$scratch/inside.sh"
cp "$here/bench.lua" "$scratch/hypr.lua"
cp "$here/bench-bars.qml" "$scratch/bars.qml"
cp "$here/bench-walk.py" "$scratch/walk.py"
cp "$root/hypr/tarn.lua" "$scratch/tarn.lua"
# Stub programs go first on PATH inside the bench: gpu-screen-recorder must never run for real.
mkdir "$scratch/bin"
cp "$here/bench-stubs/gpu-screen-recorder" "$here/bench-stubs/notify-send" "$scratch/bin/"
cp "$root/tests/support/stubs/fixtures/gpu-screen-recorder/sample.mp4" "$scratch/sample.mp4"

echo "bench: window opens on the host desktop at $(date +%T)"

# The nested compositor's world. `timeout` is the backstop; the inside script ends the
# run itself by asking the nested Hyprland to exit.
env -u HYPRLAND_INSTANCE_SIGNATURE -u HYPRLAND_CMD -u DISPLAY \
    XDG_RUNTIME_DIR="$scratch/r" HOME="$scratch/home" WAYLAND_DISPLAY="$host_wl" \
    XDG_CONFIG_HOME="$scratch/home/.config" XDG_DATA_HOME="$scratch/home/.local/share" \
    XDG_CACHE_HOME="$scratch/home/.cache" XDG_STATE_HOME="$scratch/home/.local/state" \
    BENCH="$scratch" BENCH_RUN="$scratch/r" TARN="$tarn" PATH="$scratch/bin:$(dirname "$tarn"):$PATH" \
    dbus-run-session -- timeout "${1:-480}" Hyprland -c "$scratch/hypr.lua" \
    > "$scratch/hyprland.out" 2>&1 || true

echo "bench: window closed at $(date +%T)"

# Leave nothing running: anything still holding the scratch folder is ours.
if pgrep -f "$scratch" > /dev/null; then
    pkill -f "$scratch" || true
    echo "bench: stopped leftover processes" >&2
fi

echo "bench: results in $scratch"
