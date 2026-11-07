#!/bin/sh
# Lints Tarn's QML. Called by scripts/check.sh.
#
# 1. A WheelHandler names its acceptedDevices. Qt reports Hyprland's pointer as a
#    touchpad, which a WheelHandler declines by default, so without the line every
#    scroll does nothing on the desktop while off-screen tests pass (docs/hyprland.md).
#
# 2. Raw design values in Tarn's QML: sizes, spacing, radii, positions, colors and
# durations belong in src/ui/qml/Style.qml, named, and are used from there.
#
# Tarn's QML predates this check, so it is a ratchet: scripts/qml-raw-values.baseline
# holds each file's count of lines with raw values when the check arrived, and a file fails
# only when it has MORE.
#   scripts/qml-lint.sh             check (exit 1 on a file over its count)
#   scripts/qml-lint.sh --counts    print every file's current count, baseline format
#   scripts/qml-lint.sh --tighten   lower each line of the baseline to the file's current
#                                   count (never raises one). Run it on main once no
#                                   open branch still needs the higher counts.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
baseline=scripts/qml-raw-values.baseline

# Sizes, spacing, radii and positions (0 and 1 are not design values), then raw
# colors and durations.
size='(^|[^A-Za-z0-9_])([A-Za-z.]*([wW]idth|[hH]eight|[mM]argins?|[pP]adding|[sS]pacing|[rR]adius|[sS]ize)|x|y)[[:space:]]*:[[:space:]]*-?([2-9]|[0-9]{2,}|[0-9]*\.[0-9]*[1-9])[0-9.]*[[:space:]]*(;|$|//)'
value='#[0-9a-fA-F]{6,8}\b|Qt\.rgba\([[:space:]]*[0-9.]+[[:space:]]*,[[:space:]]*[0-9.]+[[:space:]]*,[[:space:]]*[0-9.]+[[:space:]]*,[[:space:]]*[0-9.]+[[:space:]]*\)|bezierCurve:|\bduration:[[:space:]]*[0-9]'

raw_lines() { grep -nE "$size|$value" "$1" || true; }

files=$(find src/ui/qml -name '*.qml' ! -name Style.qml | sort)

if [ "${1:-}" = --counts ]; then
    for f in $files; do
        n=$(raw_lines "$f" | grep -c . || true)
        [ "$n" -gt 0 ] && printf '%s %s\n' "$n" "$f"
    done
    exit 0
fi

if [ "${1:-}" = --tighten ]; then
    tmp=$(mktemp)
    while read -r allowed f; do
        case $allowed in '#'*) printf '%s %s\n' "$allowed" "$f"; continue ;; esac
        if [ -f "$f" ]; then
            n=$(raw_lines "$f" | grep -c . || true)
            [ "$n" -lt "$allowed" ] && allowed=$n
            [ "$allowed" -gt 0 ] && printf '%s %s\n' "$allowed" "$f"
        fi
    done < "$baseline" > "$tmp"
    mv "$tmp" "$baseline"
    exit 0
fi

status=0

# 1. Each WheelHandler block (from "WheelHandler {" to its closing brace) names acceptedDevices.
for f in $files; do
    awk '
        !depth && /(^|[^A-Za-z0-9_])WheelHandler[[:space:]]*\{/ { start = FNR; depth = 0; seen = 0; open = 1 }
        open {
            if ($0 ~ /acceptedDevices/) seen = 1
            depth += gsub(/\{/, "{") - gsub(/\}/, "}")
            if (depth <= 0) {
                if (!seen) { printf "%s:%d: WheelHandler without acceptedDevices. Add acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad (Qt calls Hyprland'"'"'s pointer a touchpad; docs/hyprland.md)\n", FILENAME, start; bad = 1 }
                open = 0; depth = 0
            }
        }
        END { exit bad }
    ' "$f" >&2 || status=1
done

# 2. Raw values, against the baseline.
for f in $files; do
    n=$(raw_lines "$f" | grep -c . || true)
    allowed=$(awk -v f="$f" '$2 == f { print $1 }' "$baseline")
    allowed=${allowed:-0}
    if [ "$n" -gt "$allowed" ]; then
        echo "$f: $n lines with raw values, $allowed allowed. Take each new one from Style (src/ui/qml/Style.qml), adding a named value there if none fits:" >&2
        raw_lines "$f" | sed 's/^/    /' >&2
        status=1
    fi
done
exit $status
