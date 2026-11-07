# Shared by every stub; sourced as `. "$(dirname "$0")/_lib.sh"`. Not a stub itself.
#
# A stub is ONE executable file in this folder, named after the program it
# stands in for (copy stubs/notify-send to start a new one). It must:
#   stub_begin "$@"      log the call; exits 1 if the test put this stub in "fail" mode
#   ...                  then do its fake job, using stub_fixture for canned output
# Nothing else needs registering: the harness puts this whole folder on PATH.
#
# Everything a test controls lives under $TARN_STUB_STATE (set by the harness):
#   calls/<stub>.log          one line per call, arguments joined by the 0x1f byte
#   stdin/<stub>.<n>          standard input of call n (only for stubs that save it)
#   mode/<stub>               "ok" (default), "fail", or anything the stub understands
#   fixtures/<stub>/<file>    overrides tests/support/stubs/fixtures/<stub>/<file>

STUB_NAME=$(basename "$0")
STUB_HOME=$(dirname "$0")
: "${TARN_STUB_STATE:?TARN_STUB_STATE is not set: stubs only run inside the test harness or scripts/offscreen.sh}"

stub_mode() {
    if [ -r "$TARN_STUB_STATE/mode/$STUB_NAME" ]; then cat "$TARN_STUB_STATE/mode/$STUB_NAME"; else echo ok; fi
}

# stub_fixture <file>: path of the canned file; a test's override comes first.
stub_fixture() {
    if [ -e "$TARN_STUB_STATE/fixtures/$STUB_NAME/$1" ]; then
        echo "$TARN_STUB_STATE/fixtures/$STUB_NAME/$1"
    else
        echo "$STUB_HOME/fixtures/$STUB_NAME/$1"
    fi
}

stub_begin() {
    mkdir -p "$TARN_STUB_STATE/calls"
    _line=
    for _arg in "$@"; do _line="$_line$(printf '\037')$_arg"; done
    printf '%s\n' "${_line#?}" >> "$TARN_STUB_STATE/calls/$STUB_NAME.log"
    if [ "$(stub_mode)" = fail ]; then
        echo "$STUB_NAME: stub is in fail mode" >&2
        exit 1
    fi
}

# stub_save_stdin: keep this call's standard input for the test to read.
stub_save_stdin() {
    mkdir -p "$TARN_STUB_STATE/stdin"
    _n=$(wc -l < "$TARN_STUB_STATE/calls/$STUB_NAME.log")
    cat > "$TARN_STUB_STATE/stdin/$STUB_NAME.$_n"
}
