#!/bin/sh
# Runs INSIDE the nested Hyprland (started by bench.lua from hyprland.start), so its
# hyprctl, grim and tarn find the nested instance through the variables Hyprland sets.
# Not meant to be run by hand. Needs BENCH, BENCH_RUN and TARN from scripts/bench.sh.
set -u

# Refuse to talk to any Hyprland that is not the nested one.
case ${XDG_RUNTIME_DIR:-} in
    "$BENCH_RUN") ;;
    *) echo "bench: not in the private runtime folder, refusing to run hyprctl" >&2; exit 3 ;;
esac
for dir in "${XDG_CONFIG_HOME:-}" "${XDG_DATA_HOME:-}" "${XDG_CACHE_HOME:-}" "${XDG_STATE_HOME:-}" "${HOME:-}"; do
    case $dir in
        "$BENCH"/*) ;;
        *) echo "bench: $dir is not under the scratch folder, refusing to run" >&2; exit 3 ;;
    esac
done
[ -d "$XDG_RUNTIME_DIR/hypr/${HYPRLAND_INSTANCE_SIGNATURE:-none}" ] || { echo "bench: no nested instance" >&2; exit 3; }

hyprctl output create headless BENCH-1
hyprctl output create headless BENCH-2
# reserved takes top, right, bottom, left; distinct values show the order it is reported in.
hyprctl eval 'hl.monitor({output="WAYLAND-1", disabled=true}) hl.monitor({output="BENCH-1", mode="1280x720@60", position="0x0", scale=1, reserved={top=11, right=22, bottom=33, left=44}}) hl.monitor({output="BENCH-2", mode="1920x1080@60", position="1280x0", scale=2})'
# Monitor changes land about a second later.
sleep 1

hyprctl monitors -j > "$BENCH/monitors.json"
hyprctl binds -j > "$BENCH/binds.json"

# Real layer-shell bars on BENCH-2 (top 30, bottom 20, left 10): they give it a real
# reserved top for the Countdown, and read which slot of `reserved` is which edge.
# Skipped when quickshell is not installed.
if command -v quickshell > /dev/null; then
    quickshell -p "$BENCH/bars.qml" > "$BENCH/bars.log" 2>&1 &
    bars=$!
    sleep 3
    hyprctl monitors -j > "$BENCH/monitors-bars.json"
fi

# The walk starts, stops and times the daemon itself (scripts/bench-walk.py).
python3 -I "$BENCH/walk.py" > "$BENCH/walk.out" 2>&1
echo "walk exit=$?" >> "$BENCH/walk.out"

kill "${bars:-0}" 2> /dev/null
hyprctl dispatch 'hl.dsp.exit()'
