#!/bin/sh
# PROTOTYPE launcher for "How the Thumbnails come back after a capture" (rockylabsdev/tarn#3). Throwaway.
#   ./run.sh [A|B|C|D] [35|116|402]   one window on a made-up desktop; Space takes a capture
# Adds a Hyprland window rule once per session (no open animation, floating), as Tarn's windows have.
set -e
here=$(cd "$(dirname "$0")" && pwd)
marker="/tmp/tarn-return-rule-${HYPRLAND_INSTANCE_SIGNATURE:-none}"
if [ ! -e "$marker" ] && command -v hyprctl >/dev/null; then
    hyprctl eval 'hl.window_rule({ match = { title = "^(Tarn thumbnail return prototype)$" }, no_anim = true, float = true, center = true })' >/dev/null && touch "$marker"
fi
QT_FORCE_STDERR_LOGGING=1 exec qml6 "$here/Main.qml" -- live "${1:-B}" "${2:-116}"
