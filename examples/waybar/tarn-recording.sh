#!/bin/sh
# Waybar module for Tarn: shows a running Recording, click to stop it.
# Needs socat and jq. Protocol: docs/shell-socket.md in Tarn's repo.
#   tarn-recording.sh        follow the Tarn daemon (Waybar's "exec")
#   tarn-recording.sh stop   stop the Recording (Waybar's "on-click")
#
# Copy this file to ~/.config/waybar/, add "custom/tarn" to a modules list in
# ~/.config/waybar/config.jsonc, and define the module:
#   "custom/tarn": {
#     "exec": "~/.config/waybar/tarn-recording.sh",
#     "return-type": "json",
#     "on-click": "~/.config/waybar/tarn-recording.sh stop"
#   }
# It shows a dot while recording, a pause mark while paused and "GIF 42%" while
# a GIF is made, and nothing otherwise. Waybar sets each state as a CSS class
# (recording, paused, encoding) on #custom-tarn, for styling.
sock="${XDG_RUNTIME_DIR:?}/tarn/shell.sock"

if [ "$1" = stop ]; then
    echo '{"stop":true}' | socat - "UNIX-CONNECT:$sock"
    exit
fi

# One JSON line per change of state. Empty text hides the module. When the
# daemon isn't running (or restarts), show nothing and try again every 2 s.
while :; do
    socat -u "UNIX-CONNECT:$sock" - 2>/dev/null | jq --unbuffered -c '
        if   .recording == "recording" then {text: "●", class: "recording", tooltip: "Tarn: recording. Click to stop"}
        elif .recording == "paused"    then {text: "⏸", class: "paused",    tooltip: "Tarn: paused. Click to stop"}
        elif .recording == "encoding"  then {text: "GIF \((.progress // 0) * 100 | floor)%", class: "encoding", tooltip: "Tarn: making the GIF"}
        else {text: ""} end'
    echo '{"text":""}'
    sleep 2
done
