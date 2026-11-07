#!/bin/sh
# Run a command in the safe "off-screen" environment: scratch HOME and XDG
# folders, stub grim/hyprctl/wl-copy/notify-send first on PATH, Qt offscreen, no
# compositor reachable. Nothing it runs can touch the real desktop, clipboard,
# ~/Pictures or the real tarn socket.
#
#   scripts/offscreen.sh <scratch-dir> <command> [args...]
#
# <scratch-dir> is created if missing; use the same one for the daemon and its
# client so they share a socket. `tarn` on PATH is the built binary (BUILD_DIR,
# default build). See docs/agents/verify.md.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
build=${BUILD_DIR:-build}

[ $# -ge 2 ] || { echo "usage: $0 <scratch-dir> <command> [args...]" >&2; exit 2; }
scratch=$1
shift
case "$scratch" in
    /*) ;;
    *) echo "scratch dir must be an absolute path" >&2; exit 2 ;;
esac
if [ "$scratch" = "$HOME" ] || [ "$scratch" = / ]; then
    echo "refusing to use $scratch as a scratch dir" >&2
    exit 2
fi

mkdir -p "$scratch/home" "$scratch/run" "$scratch/config" "$scratch/data" \
    "$scratch/state" "$scratch/cache" "$scratch/sysbin" "$scratch/stubs"
chmod 700 "$scratch/run"

# Real tools the stubs need; nothing else from the system is on PATH.
while read -r tool; do
    case "$tool" in ''|'#'*) continue ;; esac
    real=$(PATH=/usr/bin:/bin command -v "$tool") || { echo "missing tool: $tool" >&2; exit 1; }
    ln -sf "$real" "$scratch/sysbin/$tool"
done < "$root/tests/support/safe-tools.txt"

[ -x "$root/$build/src/app/tarn" ] || { echo "tarn is not built: run scripts/check.sh first" >&2; exit 1; }
ln -sf "$root/$build/src/app/tarn" "$scratch/sysbin/tarn"

printf 'XDG_PICTURES_DIR="$HOME/Pictures"\nXDG_VIDEOS_DIR="$HOME/Videos"\n' > "$scratch/config/user-dirs.dirs"

unset WAYLAND_DISPLAY HYPRLAND_INSTANCE_SIGNATURE DISPLAY XDG_DATA_DIRS XDG_CONFIG_DIRS
export HOME="$scratch/home"
export XDG_CONFIG_HOME="$scratch/config"
export XDG_DATA_HOME="$scratch/data"
export XDG_STATE_HOME="$scratch/state"
export XDG_CACHE_HOME="$scratch/cache"
export XDG_RUNTIME_DIR="$scratch/run"
export PATH="$root/tests/support/stubs:$scratch/sysbin"
export TARN_STUB_STATE="$scratch/stubs"
export QT_QPA_PLATFORM=offscreen
export QT_QUICK_BACKEND=software
export QT_QPA_PLATFORMTHEME=generic

exec "$@"
