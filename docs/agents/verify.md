# Verifying Tarn: build, check, run off-screen

The desktop is shared with the user. An agent never opens a Tarn window, never runs the real `grim`, `wl-copy` or `hyprctl`, and never touches `~/.config/hypr`, the real clipboard, `~/Pictures`, `~/Videos` or the real `$XDG_RUNTIME_DIR/tarn` socket. Everything below keeps to that.

## 1. The check (always)

```sh
scripts/check.sh
```

Runs a few source checks first (QML raw values and `WheelHandler` devices, wheel events in tests, slow programs run async, tests on the daemon's clock: `CODING_STANDARDS.md` lists them), then configures (CMake + Ninja into `build/`), builds, and runs every test with CTest. Exits non-zero on any failure, and prints failing test output. The git hooks in `.githooks/` run the same script before a commit, before a conflict-free merge, and before a push (pre-push skips commits whose files already passed), so a failing commit is never recorded or pushed. CI runs it again on every PR and every push to main (`.github/workflows/check.yml`).

It keeps a list of file sets (git trees) that passed. If the files on disk are already in that list, or differ from a HEAD that passed only in Markdown, `docs/` or `.claude/`, it stops at once. So the commit hook after a check by hand costs nothing; just commit. It runs itself at low CPU and disk priority, and tests see only the fonts CI has (Noto), so a picture drawn in a font only the desktop has fails here as it would on GitHub.

`BUILD_DIR=build-foo scripts/check.sh` uses another build folder. Run one area with `scripts/check.sh --only fullscreen` (a ctest regex). It builds first, so the test binaries are never stale, and gives the tests the same fonts and low priority as the full check. It never counts as a pass. Other builds or tests you run by hand go under `nice -n 15`.

A test that fails in a full run but passes when rerun with `--only` usually failed because the machine was busy: several checks at once make timing tests flaky. Name it in your report, and fix it by waiting for the settled state (section 2) if it's a test you're working on.

## 2. How tests work

Tests drive the daemon's C++ core (`tarn::Daemon`) directly, below QML, and assert on what is visible from outside: files and their names, the arguments outside programs were run with, notifications, JSON lines on the socket. Never on which class did the work.

The fixture is `tarn::test::TestEnv` (`tests/support/test_env.h`). Create one per test in `init()`. It gives you:

- a scratch directory holding `HOME` and every `XDG_*` folder (`XDG_RUNTIME_DIR` is mode 0700); Pictures is `env.pictures()`;
- `PATH` set to `tests/support/stubs` plus the few real tools in `tests/support/safe-tools.txt`. A program with no stub (even one installed on this machine, like `slurp`) is simply not found;
- `WAYLAND_DISPLAY`, `HYPRLAND_INSTANCE_SIGNATURE` and `DISPLAY` unset, and Qt offscreen with the software renderer and no GTK theme (set by CTest);
- `env.daemon()`, a `Daemon` whose clock is `env.setNow(...)`; `env.request({"capture", "fullscreen"})` sends what the `tarn` client sends; `env.daemon().service<T>()` reaches any service (for example `ThumbnailStack`).

Reading and steering stubs, by program name:

| Call | Meaning |
|---|---|
| `env.calls("grim")` | every call's arguments, in order (`QList<QStringList>`) |
| `env.stdinOfCall("wl-copy", 1)` | what was piped into call 1 (stubs that save stdin) |
| `env.setMode("grim", "fail")` | the stub logs the call and exits 1 |
| `env.setFixture("hyprctl", "monitors.json", bytes)` | replaces a canned file for this test |
| `env.setPointer(x, y)` | shortcut for hyprctl's `cursorpos.json` |

### UI tests: input as Hyprland sends it, and the settled state

- Wheel events go through `tarn::test::sendWheel` (`tests/support/hyprland_input.h`), which comes from a pointer like Hyprland's: Qt reports it as a touchpad (`docs/hyprland.md`). The check refuses a `QWheelEvent` built anywhere else in `tests/`.
- Keys reach a window only once it has focus: `QTRY_VERIFY(view->activeFocusItem())` before the first `QTest::keyClick`.
- Wait for the final state with `QTRY_*` on the value the user ends up seeing ("1 capture", not the "1 capture · reading 1" that comes first while text reading runs). An assertion on the first state passes alone and fails when the machine is busy.

### Reference images

Drawing tests compare against PNGs in `tests/<area>/reference/`, pixel for pixel. After an intended change to drawing, regenerate them with `TARN_UPDATE_REFERENCES=1 scripts/check.sh --only <area>`: through the check, so they are drawn with the fonts CI has. Then look at every changed image before committing.

### Adding a stub (one file)

Copy `tests/support/stubs/notify-send` to `tests/support/stubs/<program>`, `chmod +x`, and write its fake job. It starts with `. "$(dirname "$0")/_lib.sh"` and `stub_begin "$@"`, which logs the arguments and honors fail mode. `_lib.sh` documents the helpers: `stub_fixture <file>` (canned output, a test's override first), `stub_save_stdin`, `stub_mode`. Canned files go in `tests/support/stubs/fixtures/<program>/`. Nothing else needs registering. Existing stubs: `grim`, `hyprctl` (cursorpos, monitors, binds and clients queries answered, in the shapes the nested bench read from Hyprland: `docs/hyprland.md`; `dispatch <name> …`, `keyword` and an `eval` rule pattern not anchored with `^…$` fail as on the real desktop; everything else only logged), `notify-send`, `wl-copy`, `tarn-clip` (the clipboard helper: only records its arguments), `xdg-open` (`tarn settings`; only records the file it was asked to open), `gpu-screen-recorder` (see below), `gifski` (see below), `ffmpeg` (see below), `ZXingReader` (prints `fixtures/ZXingReader/codes.txt`, one QR code's contents per line, empty by default; keeps the picture it was given as `env.stdinOfCall("ZXingReader", n)`).

**The recorder stub** writes the tiny real MP4 `fixtures/gpu-screen-recorder/sample.mp4` (6 s, 64x48) to its `-o` path after `start_delay` seconds (fixture file, default 0.3; the real one needs ~0.45 s), then runs until SIGINT. `env->setMode("gpu-screen-recorder", "die")` makes it exit at once, `"silent"` never writes the file. Signals it gets other than INT are logged to `$TARN_STUB_STATE/signals/gpu-screen-recorder.log`. `ffmpeg` and `ffprobe` are real (in `safe-tools.txt`): they read the sample for the poster frame and the `MP4 0:06` length. After a call to the stub, wait until the session is `Recording` before reading `env->calls("gpu-screen-recorder")`: the process logs its arguments a moment after it starts.

**The gifski stub** reads the y4m stream ffmpeg pipes to it (kept: `env.stdinOfCall("gifski", 1)`), prints `\rFrame i / N ____` progress like the real one, and writes the tiny `fixtures/gifski/sample.gif` to its `-o` path. Fixtures `frames` (default 5) and `frame_delay` (seconds, default 0.02) steer the progress; `env.setMode("gifski", "die")` prints an error and exits 1. `ffmpeg` is the stub described next, which runs the real one by default.

**The ffmpeg stub** logs every call (`env->calls("ffmpeg")`) and then runs the REAL ffmpeg with the same arguments, so poster frames, previews and Trim cuts are real on the tiny fixtures. `fixtures/gpu-screen-recorder/sample60.mp4` is a 3 s, 64x48, 60 fps clip where frame N is painted as N in binary (eight black/white blocks): tests decode a result and read the frame numbers back to prove a cut is frame-exact (`tests/trim`). Mode `"fake"` runs no encoder: it prints `out_time_us=` values from fixture `progress_us` (space separated, default `1000000 2000000`), `step_delay` seconds apart (fixture, default 0.05), then copies the `-i` file to the last argument; a large `step_delay` gives a slow trim to watch and Cancel. Mode `"error"` prints a line on stderr and exits 1.

**The Shell socket** (`tests/shell`): the daemon listens on `env.runtimeDir() + "/tarn/shell.sock"` (scratch, never the real one). A stand-in shell is a `QLocalSocket` that reads JSON lines; see `FakeShell` in `tests/shell/shell_test.cpp`.

### Adding a module or a test area (no shared lines to edit)

- A core module is a folder `src/core/<module>/` with a one-line `CMakeLists.txt` (`tarn_core_module()`); every `.cpp`/`.h` in it is picked up. A module that adds commands registers them from its own file with `TARN_MODULE(name, init)` (see `src/core/capture/capture_fullscreen.cpp`) and reaches shared services with `daemon.service<T>()`; a service is any `QObject` with a `T(Daemon &)` constructor, created on first use.
- A test area is a folder `tests/<area>/` with `CMakeLists.txt` containing `tarn_add_test(<area>)`; all its `.cpp` files become the executable `test_<area>`, registered with CTest. Add `LIBS tarn_ui` for QML tests.
- QML files go in `src/ui/qml/*.qml` and assets in `src/ui/assets/`; both are compiled into the binary (module `Tarn.Ui`, loaded from `qrc:/qt/qml/Tarn/Ui/qml/<File>.qml`). Nothing is read from disk at run time.
- After adding a folder, a plain `ninja -C build` notices it.

Outside programs run through `ProcessRunner` (blocking; fine for the tens-of-milliseconds commands of a capture). A long job such as a recording needs its own async `QProcess`, and its stub must still be on the test `PATH`. A job that can take long but has a single answer (Tesseract, `ffprobe`, a poster frame) uses `ProcessRunner::runAsync(program, args, input, timeoutMs, context, done)`: `done` runs on the event loop, so tests wait on the visible result with `QTRY_*` (for example `TextCapture::busy()`, the Recording reaching `Done`). Stubs can be slowed to prove the daemon is not blocked: `tesseract` mode `slow`, `ffmpeg` fixture `poster_delay` (seconds).

## 3. Running the daemon off-screen

`scripts/offscreen.sh <scratch-dir> <command...>` runs any command in the same safe environment the tests use: scratch `HOME` and XDG folders, stubs first on `PATH`, Qt offscreen with the software renderer, no compositor reachable. `tarn` on that `PATH` is the binary built by `scripts/check.sh`. Use one scratch dir for the daemon and its client so they share a socket.

```sh
scripts/check.sh                                   # builds tarn
S=$(mktemp -d /tmp/tarn-scratch-XXXXXX)
scripts/offscreen.sh $S tarn daemon > $S/daemon.log 2>&1 &
sleep 1
scripts/offscreen.sh $S tarn capture fullscreen    # exit 0
tr '\037' ' ' < $S/stubs/calls/grim.log            # grim's arguments, one call per line
ls $S/home/Pictures/Screenshots                    # after Save is invoked (tests do this)
scripts/offscreen.sh $S tarn quit                  # stops the daemon
rm -rf $S
```

Exit codes of the client: 0 done, 1 the daemon refused (message on stderr), 2 no daemon running or no answer.

Never start `tarn daemon` or `tarn capture` outside `scripts/offscreen.sh`: on the real session it would put a window on the user's monitor and use the real socket.

## 3b. The nested bench (real Hyprland, only when a ticket says so)

`scripts/bench.sh [seconds]` runs a Hyprland inside the user's session with a 1x (1280x720) and a 2x (1920x1080, with three bars) headless output, loads the real `hypr/tarn.lua`, and runs `scripts/bench-walk.py` inside it: Overlay and Thumbnail open times on each output, then every surface (Overlay, Thumbnail, Countdown, Recording bar, History, Editor, Pin) on both, once with Qt Quick on the CPU software renderer and once on OpenGL through llvmpipe. It also runs `tarn doctor` there. Results (`walk.json`, `walk.out`, `events.log`, `shots/*.png`, logs) go to a scratch folder it prints. It is an exception to the rule that an agent opens no window: **one nested window appears on the real desktop for the length of the run** (about three minutes), because Hyprland cannot start without a host compositor. Run it only when a ticket allows it, tell the user when it starts and ends, and keep the runs few and short. `TARN=<path>` picks the binary (default `build/src/app/tarn`): build it first, as a skipped check builds nothing.

It reaches nothing of the host: `dbus-run-session`, a private `XDG_RUNTIME_DIR`, `HOME` and all four `XDG_*_HOME` folders (the host's beat `HOME`), `DISPLAY` unset, stub `gpu-screen-recorder` and `notify-send` first on `PATH`, and both scripts refuse to run when any of those is outside the scratch folder. Input is only Tarn's own client commands, the nested `hyprctl` and a virtual keyboard in the nested compositor. The compositor itself still draws on the host's GPU, and `gpu-screen-recorder` cannot see a nested output, so the bench shows layout, scales, open times and Qt's CPU rendering, not recording or compositor cost. Its traps (short scratch path, the event socket as a clock, what the software renderer cannot draw) are in `docs/hyprland.md`. A crash of the nested Hyprland leaves a report in `~/.cache/hyprland/`.

## 4. needs-eyes checks (the user, on the real monitor)

Some things only a person at the real monitor can judge: how it looks and moves, and real Hyprland behavior. A ticket lists them as **needs-eyes**; the agent does not do them, and reports them. The user does them like this:

1. Build (`scripts/check.sh`) and link the binary onto `PATH`: `ln -sf $PWD/build/src/app/tarn ~/.local/bin/tarn`.
2. Load the Hyprland file: copy `hypr/tarn.lua` to `~/.config/hypr/config/tarn.lua` and add `require("config.tarn")` at the end of `~/.config/hypr/hyprland.lua` (README, "Set up Hyprland"; a build from source installs the same file to `/usr/share/doc/tarn/tarn.lua`). Then run `hyprctl reload` and `tarn daemon &`, and check with `tarn doctor`. After editing `hypr/tarn.lua`, copy it again: the copy is not a link.
3. Press the key or run the command under test and check what the ticket's needs-eyes line says (for example Super+Print gives a pixel-identical PNG with no pointer in it, and the Thumbnail appears with no fade).

### Writing a needs-eyes item

The user does the item without the agent there, so it names everything. Vague items had the user testing the wrong thing ("over a browser tab") and looking in the wrong folder (History's own copies instead of `~/Pictures/Screenshots`). Write each item as:

- **Setup:** the state to start from (which app is open, which Setting, what to capture first).
- **Do:** the exact keys, clicks or command.
- **Expect:** what passes, in terms the user can see.
- **Where to look:** the window, folder (full path) or notification that shows it.

For example: *Setup: a white page fills the screen. Do: Super+Print, Edit, add a text Watermark. Expect: the Watermark reads clearly on the white. Where to look: the Editor, then the PNG in `~/Pictures/Screenshots`.*

### Editing a checklist the user ticks

The user ticks items on GitHub by hand, so a checklist's only copy is the live one. Change it through `scripts/gh-edit.py` (`--help` for usage), which edits the live text and refuses to save over an edit made in the meantime. Use `--tick`, `--replace <old> <new>` with files written by Write, or `--append`. A PATCH of a local copy once wiped the user's ticks.

## 5. Feature notes

Each feature with its own test seams has a file in `docs/agents/features/`: its services, how its tests drive it, and its needs-eyes parts. Before working on a feature, read its file. `head -3 docs/agents/features/*.md` shows what each one covers.

A ticket that adds a feature adds its own file there, starting with a `# Title` line and a "Read when working on …" line, and puts a new rule for an existing feature in that feature's file. Each feature has its own file, so parallel tickets don't collide in this one.
