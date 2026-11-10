#!/bin/sh
# PROTOTYPE (rockylabsdev/tarn#3, throwaway): every return variant off-screen, frame by
# frame, into one contact sheet. Nothing appears on the desktop.   ./shoot.sh [out folder]
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$(realpath -m "${1:-$here/shots}")
mkdir -p "$out/frames"
QT_FORCE_STDERR_LOGGING=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
    qml6 "$here/Main.qml" -- shot "$out/frames"
labels="before|hidden|back|+50ms|+100ms|+200ms|+350ms|+600ms"
for v in A B C D; do
    i=0
    for l in $(echo "$labels" | tr '|' ' '); do
        magick "$out/frames/$v-$i.png" -crop 560x700+0+200 +repage -resize 50% \
            -gravity north -background '#222' -fill white -splice 0x22 -font "$here/../../src/ui/assets/fonts/Geist-Medium.otf" -pointsize 14 -annotate +0+3 "$v  $l" "$out/frames/c-$v-$i.png"
        i=$((i + 1))
    done
    magick "$out"/frames/c-$v-[0-7].png +append "$out/row-$v.png"
done
magick "$out"/row-[ABCD].png -append "$out/sheet.png"
echo "$out/sheet.png"
