#!/bin/sh
# The one check: configure, build, run every test. Non-zero on any failure.
# Used by the pre-commit hook (.githooks/pre-commit) and by agents (docs/agents/verify.md).
#   BUILD_DIR=build-foo scripts/check.sh    use another build folder (default: build)
#   scripts/check.sh --only trim            build, then run only the test areas matching
#                                           the ctest regex; never counts as a pass
set -eu

only=
if [ "${1:-}" = --only ]; then
    only=${2:?"usage: scripts/check.sh --only <ctest regex>"}
fi

root=$(cd "$(dirname "$0")/.." && pwd)
build=${BUILD_DIR:-build}
cd "$root"

# Low priority for CPU and disk, for this script and everything it starts: the
# check runs on the user's desktop, often several at once, and must not make it lag.
renice -n 15 -p $$ >/dev/null 2>&1 || true
if command -v ionice >/dev/null; then ionice -c 2 -n 7 -p $$ 2>/dev/null || true; fi

# The git tree of the files on disk, untracked ones included (a scratch index,
# so the real one is untouched), and the list of trees that passed this check.
# git hashes the same files to the same tree, so a tree in the list has passed.
passed="$(git rev-parse --git-common-dir)/tarn-checked-trees"
disk_tree() {
    scratch_index=$(mktemp)
    cp "$(git rev-parse --git-path index)" "$scratch_index" 2>/dev/null || true
    GIT_INDEX_FILE=$scratch_index git add -A
    GIT_INDEX_FILE=$scratch_index git write-tree
    rm -f "$scratch_index"
}
tree=$(disk_tree)
if [ -z "$only" ] && [ -f "$passed" ] && grep -qx "$tree" "$passed"; then
    echo "check: these exact files already passed; nothing to do." >&2
    exit 0
fi
# Markdown, docs/ and .claude/ are never built or tested: when only they differ
# from a commit that passed, the check passes without building.
head_tree=$(git rev-parse -q --verify 'HEAD^{tree}' || true)
if [ -z "$only" ] && [ -n "$head_tree" ] && [ -f "$passed" ] && grep -qx "$head_tree" "$passed" &&
   ! git diff --name-only "$head_tree" "$tree" | grep -qvE '\.md$|^docs/|^\.claude/'; then
    echo "check: only docs changed since HEAD, which passed; nothing to build." >&2
    echo "$tree" >> "$passed"
    exit 0
fi

# Whatever launched this (a hook, an agent shell on the live desktop), the tests
# must not be able to reach the real compositor.
unset WAYLAND_DISPLAY HYPRLAND_INSTANCE_SIGNATURE DISPLAY

# Qt's AUTOMOC misses headers in modules added since the last configure, so an
# incremental build after a merge that adds a module fails to link
# ("undefined reference to vtable"). Start the build folder afresh whenever the
# set of source files changes; an unchanged set keeps the fast incremental build.
stamp="$build/.tarn-sources"
sources=$(git ls-files --cached --others --exclude-standard src tests cmake CMakeLists.txt | sort | sha256sum)
if [ -d "$build" ] && { [ ! -f "$stamp" ] || [ "$(cat "$stamp")" != "$sources" ]; }; then
    rm -rf "$build"
fi

# ccache, when installed, keeps compiled files across fresh build folders and
# worktrees (BASEDIR makes its keys independent of where the checkout lives).
launcher=
if command -v ccache >/dev/null; then
    launcher="-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache"
    export CCACHE_BASEDIR="$root"
fi

# docs/agents/map.md names every core module, so a new one can't leave it stale.
for module in src/core/*/; do
    module=$(basename "$module")
    if ! grep -q "^- \`$module/\`:" docs/agents/map.md; then
        echo "check: src/core/$module has no line in docs/agents/map.md; add one saying what it does" >&2
        exit 1
    fi
done

# Hyprland can close a window (Super+Q) without asking Tarn: the window just hides.
# Each window type (src/ui/*_windows.cpp; layer surfaces can't be closed so) must
# react to that, or its session stays open and the next Edit, Pin or Trim breaks.
for windows in src/ui/*_windows.cpp; do
    if ! grep -q 'QWindow::visibleChanged' "$windows"; then
        echo "check: $windows never handles QWindow::visibleChanged; Hyprland can close the window behind Tarn's back (CODING_STANDARDS.md)" >&2
        exit 1
    fi
done

# Tests fake the daemon's clock (TestEnv::now()), so a test that reads the real
# one passes until the calendar moves past the fake date, then fails for good.
if real_clock=$(grep -rn 'QDateTime::currentDateTime' tests --include='*.cpp' --include='*.h' | grep -v '^tests/support/'); then
    printf '%s\n' "$real_clock" "check: tests date things with env->now(), the daemon's faked clock, not the real one" >&2
    exit 1
fi

# A QWheelEvent built without a device comes from a mouse, but Qt reports Hyprland's
# pointer as a touchpad: such a test passes while the desktop ignores the scroll.
if raw_wheel=$(grep -rn 'QWheelEvent' tests --include='*.cpp' --include='*.h' | grep -v '^tests/support/'); then
    printf '%s\n' "$raw_wheel" "check: send wheel events with tarn::test::sendWheel (tests/support/hyprland_input.h), which comes from a pointer like Hyprland's" >&2
    exit 1
fi

# ProcessRunner::run blocks the daemon, and every surface with it, until the
# program exits. Programs that can take seconds go through runAsync.
# `tarn doctor` (src/app/doctor.cpp) is exempt: it runs in the client, which blocks
# nothing but itself, and bounds every call with a timeout.
slow='tesseract|ffmpeg|ffprobe|gifski|gpu-screen-recorder|nice'
if blocking=$(grep -rnPzo --exclude=doctor.cpp "ProcessRunner>\(\)\s*\.run\(\s*QStringLiteral\(\"($slow)\"" src | tr '\0' '\n' | grep .); then
    printf '%s\n' "$blocking" "check: run slow programs with ProcessRunner::runAsync, not the blocking run() (CODING_STANDARDS.md)" >&2
    exit 1
fi

scripts/qml-lint.sh

# Tests see only the fonts CI installs (noto-fonts; Tarn's own load from its
# resources). The desktop has more, Geist among them, so a test drawing in a system
# font would match its reference image here and fail on GitHub.
mkdir -p "$build"
build_abs=$(cd "$build" && pwd)
printf '%s\n' '<?xml version="1.0"?>' '<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">' '<fontconfig>' \
    '  <dir>/usr/share/fonts/noto</dir>' "  <cachedir>$build_abs/fontconfig-cache</cachedir>" \
    '  <include ignore_missing="yes">/etc/fonts/conf.d</include>' '</fontconfig>' > "$build_abs/fonts.conf"
export FONTCONFIG_FILE="$build_abs/fonts.conf"

cmake -S . -B "$build" -G Ninja $launcher >/dev/null
printf '%s\n' "$sources" > "$stamp"
ninja -C "$build"
# One test area per core; each area has its own scratch folders. Twice that made
# timing tests flaky when several checks ran at once.
if [ -n "$only" ]; then
    ctest --test-dir "$build" --output-on-failure --parallel "$(nproc)" -R "$only"
    exit 0   # some areas ran, not all: nothing to record
fi
ctest --test-dir "$build" --output-on-failure --parallel "$(nproc)"

# Record the tree that passed, unless the files changed while the check ran. The
# next check of the same files (the commit hook after a check by hand) stops at the
# top, and .githooks/pre-push lets a commit with this tree through.
if [ "$(disk_tree)" = "$tree" ]; then echo "$tree" >> "$passed"; fi
