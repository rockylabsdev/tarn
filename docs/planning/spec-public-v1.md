# Tarn public v1: packaging, shell contracts and a clean public repo

[ADR 0004](../adr/0004-shell-contracts-are-files-and-a-socket.md) and [ADR 0005](../adr/0005-public-v1-is-a-new-repo-from-one-commit.md) hold the reasons for the two hard-to-reverse decisions.

## Problem Statement

Tarn works on one machine: the author's, with one monitor, a 4090 and the author's private Quickshell shell, Island. A stranger can't use it. There is no package, no license and no version number; the README is a build note; the Hyprland file talks about Island; the colors, the wallpaper and the Recording indicator only work when Island is there; a GIF of a large area is slow and huge on a weak machine; and a machine with no hardware encoder can't record at all. When something's missing, Tarn gives a one-line error and nothing to check it against.

The repo can't simply be made public either. Its history holds third-party screenshots that can't be removed for good, and its docs are written for someone who already knows the project.

## Solution

Tarn ships as `v1.0.0`: an AUR package and a GitHub release, in a new public repo at `rockylabsdev/tarn` that starts from one commit. ([ADR 0005](../adr/0005-public-v1-is-a-new-repo-from-one-commit.md) says why.)

A stranger on Arch or CachyOS with Hyprland installs the package, runs `tarn doctor` to see what's missing, copies one Lua file into their Hyprland config, and has working capture keys. Colors, the wallpaper and a Recording indicator plug in through two files and a socket that any shell can use, with Island as one client among others and a Waybar example in the docs. Tarn scales down big GIFs and falls back to CPU recording on its own. The README, `CONTRIBUTING.md` and the planning documents explain everything to a reader who has never seen Island, Lua config or the agent workflow, and nothing public describes Tarn by reference to another product.

## User Stories

### Finding and installing

1. As a Hyprland user, I want the README to show what Tarn does in 15 seconds, so that I can decide whether to try it without installing it.
2. As an Arch or CachyOS user, I want to install Tarn with `paru -S tarn`, so that I get it like any other package.
3. As a user of another distro, I want the README's source build to list every package, so that I can build it without guessing.
4. As an installer, I want the package to pull in everything Tarn needs to capture, record and show its icons, so that nothing is quietly broken on first run.
5. As an installer, I want recording to work with no password prompt, so that I don't have to set up permissions myself.
6. As a user, I want `tarn --version` to print the version, so that I can say what I'm running in a bug report.
7. As a user, I want a license file, so that I know what I may do with Tarn.
8. As a user, I want a Tarn entry in my app launcher that opens the History window, so that I can find my captures without remembering a command.
9. As a reader of the GitHub release, I want short notes with the features, requirements and install lines, so that I don't have to read a list of issues.

### Setting up Hyprland

10. As a new user, I want to copy one Lua file and add one `require` line, so that the keys, rules and autostart all work.
11. As someone new to Lua config, I want the README and the file's header to explain what the file is and where it goes, so that I don't need to know Hyprland's config language first.
12. As a user with my own binds, I want Tarn's file to only add things, and the README's key table to say which keys it takes, so that I can change a clash.
13. As a user, I want the README to explain the hardware cursor option, so that I understand why the pointer can show up in my captures.

### Knowing what's wrong

14. As a new user, I want `tarn doctor` to check my setup in one command, so that I can see everything that's missing at once.
15. As a user, I want each missing tool listed with the `pacman -S` line that fixes it, so that I don't have to find the package name.
16. As a user, I want each optional tool to say what it unlocks, so that I can choose what to install.
17. As a user, I want `tarn doctor` to tell me whether Hyprland is running, the Tarn file is loaded and the daemon answers, so that a silent failure has a clear cause.
18. As a user with no hardware encoder, I want `tarn doctor` to say Recordings will use the CPU, so that I'm not surprised by the load.
19. As a user, I want `tarn doctor` to finish within a few seconds even when a tool hangs, so that it never looks stuck.
20. As a script author, I want `tarn doctor` to exit non-zero only when something required is missing, so that I can use it in a check.
21. As a user who hits a missing-tool or no-daemon error, I want the error to point at `tarn doctor`, so that I know what to do next.

### Plugging into my desktop

22. As a matugen user, I want to point `palette_file` at a file generated from a documented template, so that Tarn's colors follow my wallpaper.
23. As a user, I want the Palette to update when the file changes, so that I don't restart anything.
24. As a user with a partly filled file, I want each missing color to fall back on its own, so that one missing key doesn't undo the rest.
25. As a user with no Palette file, or an unreadable one, I want Tarn's built-in colors, so that it still looks right.
26. As a user, I want `accent_color` to set a fixed accent, so that I can pick one without a palette tool.
27. As a shell author, I want optional `accent` and `on_accent` keys, so that my shell's accent can differ from Material's `primary`.
28. As a user, I want `wallpaper_file` to name a file holding my current wallpaper's path, so that the Background panel offers my wallpaper.
29. As a user with no wallpaper file, I want the Background panel to offer only the images in `wallpaper_folder`, so that nothing breaks.
30. As a user, I want the Editor's color popover to name its live swatches for what they are, so that I'm not left wondering what "Island" means.
31. As a user with no Palette file, I want the Palette swatches and gradients left out, so that I don't see empty or meaningless choices.
32. As a Waybar user, I want a copy-paste module that shows a recording dot, a pause mark and GIF progress and stops on click, so that I can see a Recording from my bar.
33. As a shell author, I want the Shell socket's protocol documented, so that I can write my own client.
34. As a user with a top bar, I want the Countdown to sit just under it, whatever its height, so that it never hides behind the bar.
35. As a user with no top bar, I want the Countdown near the top edge, so that it doesn't float oddly low.
36. As the author, I want Island to keep working after the rename, with my colors and wallpaper as they are, so that going public doesn't break my desktop.

### Weaker machines

37. As a user on an iGPU, I want a big GIF scaled to a sensible width, so that encoding doesn't take minutes and the file stays small.
38. As a user, I want `gif_max_width` to change that limit, or `0` to turn it off, so that I can choose quality.
39. As a user, I want the kept MP4 to stay at full size, so that the limit only affects the GIF.
40. As a user who trims a GIF Recording, I want the remade GIF to follow the same limit, so that it matches.
41. As a user with no hardware encoder, I want Recordings to work on the CPU without any setup, so that Tarn doesn't simply fail.
42. As a user with a scaled or second monitor, I want the Overlay, Thumbnail, Countdown, Recording bar and captures on the right monitor at the right size, so that Tarn is correct on setups other than the author's.
43. As a user on a modest machine, I want the Overlay to open and the Thumbnail to appear without a visible wait, so that Tarn stays quick.

### Contributing

44. As a contributor, I want a short `CONTRIBUTING.md`, so that I can build, check and send a change.
45. As a contributor, I want the git hooks and CI to run the same check, so that a problem shows up before review.
46. As a bug reporter, I want an issue form that asks for `tarn doctor` and `tarn --version`, so that I don't have to guess what's useful.
47. As someone with a feature idea, I want a form and a clear rule to open an issue first, so that I don't build something that won't land.
48. As a reader, I want a plain explanation of how the repo is planned and what the triage labels mean, so that the agent files don't confuse me.
49. As a reader who doesn't use agents, I want to be told they're optional, so that I don't feel I have to.
50. As a security researcher, I want a private way to report a problem, so that I don't have to post it in public.
51. As a reader, I want planned features listed as issues, so that I can follow scrolling capture and the Recording Editor.
52. As a reader, I want to know development before v1.0.0 happened in a private repo, so that the single first commit isn't a mystery.
53. As a reader, I want the v1, v2 and public v1 specs in `docs/planning/`, so that I can see how Tarn was planned.
54. As a reader, I want every doc and comment to describe Tarn on its own terms, so that nothing leans on another product.

### Going public

55. As the author, I want the public repo to hold no third-party screenshots and no personal details, so that nothing unintended is published.
56. As the author, I want the PKGBUILD proven in a clean chroot before it is published, so that the first public install works.

## Implementation Decisions

### Version

- CMake's project version is the only source of the number, because the source tarball has no `.git`. It goes to `1.0.0`, and a generated header feeds the client.
- `tarn --version` prints `tarn 1.0.0`, runs without the daemon, and joins the usage line along with `tarn doctor`.
- The release step stops if the tag and the CMake version differ.

### Install rules and the package

- CMake gets `install()` rules: `tarn` and `tarn-clip` to the binary directory, `tarn.desktop` to the applications directory, the Hyprland file and the Waybar example under Tarn's doc directory (`tarn.lua`, `waybar/`). The PKGBUILD installs the licenses.
- Tests become switchable through CMake's standard `BUILD_TESTING`, and the PKGBUILD turns them off. `tests/` is unconditional today.
- `tarn-clip` needs no change: the daemon finds it on `PATH`.
- `tarn.desktop` runs `tarn history`. With no daemon running it can't open anything; the README's setup starts the daemon with Hyprland, so that is a broken-setup case for `tarn doctor`.
- The PKGBUILD follows an earlier draft, with these changes: its `source` is GitHub's automatic archive for the tag (`archive/refs/tags/v$pkgver.tar.gz`), since the release uploads no files; `qt6-svg` joins `depends` (the icons need it; CI installs it, the draft omitted it); `pkgdesc` describes Tarn on its own terms; the license is MIT; the maintainer line is `Jamey Smith` with the repository's issues page; the build passes `-DBUILD_TESTING=OFF`; `sha256sums` is `SKIP` in the tree, because the tag does not exist yet; the real sum goes in when the release is cut. Add `optdepends` for `socat` and `jq` (the Waybar example) alongside `zxing-cpp`.
- `ffmpeg`, `gifski`, `tesseract` and `tesseract-data-eng` stay in `depends`, so a package install has every feature. `tarn doctor` still treats them as optional, for source builds.
- Tarn sets no capability. `gpu-screen-recorder`'s own package gives `gsr-kms-server` `cap_sys_admin`.
- The PKGBUILD lives in the Tarn repo, and the AUR repo holds a copy with `.SRCINFO`.

### `tarn doctor`

- Runs in the client, without the daemon. One line per check (✓ / ! / ✗), with a `pacman -S …` hint for each missing package. Exits non-zero only when something required is missing.
- **Required:** Hyprland running; `grim`; `gpu-screen-recorder`; the Tarn file loaded (its binds show in `hyprctl binds`); the daemon answering.
- **Optional**, each naming what it unlocks: `gifski` and `ffmpeg` (GIF, Trim); `tesseract` and its installed languages (Text capture); `ZXingReader` (QR codes); `cursor:no_hardware_cursors` (information only).
- **Encoder:** `gpu-screen-recorder --info` under a 5 s timeout reports hardware h264, CPU fallback, or "couldn't tell". Without the timeout it blocks for about 25 s on an unanswered portal.
- Every outside call has a timeout, and a hung child is killed. With no Hyprland, the checks that need `hyprctl` say so instead of failing one by one.
- It blocks the client, not the daemon, so the check's slow-program rule shouldn't apply to it; adjust that rule to say so rather than running doctor async for nothing.
- How Lua binds appear in `hyprctl binds -j` is unconfirmed. The hardware pass reads it on the nested bench and fixes the stub fixture to match. Don't assume the pre-Lua `exec` shape.
- The no-daemon and missing-tool errors keep their wording and add "run `tarn doctor` for a full check".

### The GIF width limit and CPU fallback

- `gif_max_width` Setting, default 1280, `0` off. A wider GIF is scaled down in the ffmpeg step, keeping its aspect ratio; a narrower one is never scaled up. The width is in pixels, so on a 2x monitor a 1280-wide GIF is half the Selection's pixels.
- A GIF Trim remakes to the same limit. The MP4 is always kept full size. gifski stays at `--quality 100` and 20 fps (`docs/recording.md`), and encoding stays under `nice -n 10`.
- Always pass `-fallback-cpu-encoding yes`. Tarn already passes `-k h264`, the only codec the fallback supports. The flag does nothing on a machine with an encoder.
- The recorder itself isn't niced: it records in real time. No per-Recording encoder probe.
- The Recording bar still waits for the output file, so a different start-up time on the CPU changes nothing.
- `docs/recording.md` records the limit, the flag and that its numbers are from a 4090.

### Shell contracts

- `palette_file` Setting, empty by default: a flat JSON file mapping Material 3 names to `"#rrggbb"`. Watched live, including a tool that replaces the file by renaming over it. Tarn reads 12 keys: `surface_container_lowest` … `surface_container_highest`, `surface_bright`, `on_surface`, `on_surface_variant`, `outline`, `outline_variant`, `primary`, `on_primary`. Optional `accent`/`on_accent` win over `primary`/`on_primary`. A missing key falls back on its own; an unreadable file means built-in colors. Reading `accents.json` is dropped.
- `accent_color` Setting, empty by default. Order: Setting, `accent`, `primary`, built-in.
- `wallpaper_file` Setting, empty by default: a file whose first line is the current wallpaper's path. Watched live. The `palette.source` fallback is dropped, and Tarn runs no command to find the wallpaper.
- The Shell socket is `$XDG_RUNTIME_DIR/tarn/shell.sock`, protocol unchanged, no `island.sock` alias. `docs/island.md` becomes `docs/shell-socket.md`: the protocol, plus a short section on Island as the reference client, unlinked. Island's placement rules leave the doc.
- The Countdown reads Hyprland's reserved top for its monitor when it starts and sits at reserved top + 12. That is 68 with Island. No new Setting and no exclusive zone. Add `reserved` to `docs/hyprland.md`'s query table; confirm the array's order on the installed Hyprland, and make the `hyprctl` fixture match. A monitor that reports nothing counts as 0.
- Nothing in Tarn names Island: the module becomes `shell/` (`IslandBridge` → `ShellSocket`), `Palette::fromIsland` → `fromFile`, `Palette::islandPalettePath` goes, `Style.island*` → `Style.bar*`, tests and test helpers follow.
- Two more places, found while testing these stories. The Editor's color popover shows the word "Island" as a section label: it becomes "Palette". The Frame panel offers its Palette gradients "only while Island is running": that becomes "while a Palette file is in use", and with none, the swatches and gradients are left out.
- Comments and docs that describe Island or copy its values are reworded (`Style.qml`, `Spring.qml`, `OsdFrame.qml`, the windows' header comments, surface comments, `docs/hyprland.md`, ADR 0002's links). Credit stays as "Island, the author's Quickshell shell, the reference client", unlinked.
- The Settings file's first-run template gains the new keys with comments.

### README, examples, Hyprland file

- The README, public Hyprland file and Waybar script follow an approved shape: pitch → demo GIF → features → Install → `tarn doctor` → Set up Hyprland → hardware cursor → Settings → Make it fit your desktop → Limits → Planned → How it's built → Credits → MIT.
- Lua only, no `tarn.conf`. The reader copies the installed file to `~/.config/hypr/config/` and adds `require("config.tarn")`.
- `docs/media/demo.gif`: about 15 s, 1920 px wide, recorded with Tarn on the real monitor by the user, plus three stills.
- The README's Planned list links the new repo's first two issues, scrolling capture and the Recording Editor, which are created in that order right after the first push.
- `docs/agents/verify.md`'s setup steps follow the new file and paths.

### Contributor docs

- `CONTRIBUTING.md`, under about 120 lines: build and check, report a bug, send a change, how the repo is planned, using an agent, and one line on conduct.
- The planning tour links only the shipped briefs and exported specs as examples, since the tracker from before v1 stays private. It says later ones are public on the tracker.
- `CONTRIBUTING.md` gets the line: "Development before v1.0.0 happened in a private repo; planning is public from here on."
- `docs/agents/issue-tracker.md` stays "request surface: no". All five triage labels are kept; issue forms apply `needs-triage`; `CONTRIBUTING.md` has a label table.
- Issue forms: Bug report and Feature idea, with blank issues off. Three-line `SECURITY.md`. No `CODE_OF_CONDUCT.md`.
- `.claude/` ships minus `animate` and the broken `make-interfaces-feel-better` symlink (and `skills-lock.json` follows). `THIRD-PARTY.md` gains the 23 `mattpocock/skills` skills (MIT) and humanlayer's text in `pr` (MIT, from `humanlayer/skills`), with license texts under `.claude/skills/LICENSES/`.
- Root `LICENSE`: MIT.

### `docs/planning/` and the public wording

- `docs/planning/` holds `brief.md` and `brief-v2.md`, the exported specs and a five-line README saying these are kept as history and name the tool that inspired Tarn.
- The briefs move with their links into the reference folder removed, and the Studio line in `brief-v2.md` uses Recording Editor. They otherwise stay as they were.
- The v1, v2 and this spec are exported, edited for an outside reader: no issue numbers, no product comparison, no cut-over details about the archive beyond what's in ADR 0005.
- The briefs for scrolling capture and the Recording Editor leave the repo; their text becomes the new repo's first two issues, in Tarn's own terms.
- No public text but the briefs and the planning README names the tool that inspired Tarn: `README.md`, `GLOSSARY.md`, `CLAUDE.md`, ADRs, code and test comments, QML comments.
- About 84 `#n` citations across the tree: keep the lesson, drop the number. Hex colors aren't citations; go by eye, not a blind regex.
- `CLAUDE.md`'s Planning section points at the new paths.

### Repo metadata

- Description: "Screenshots and screen recordings for Hyprland: capture, annotate, record, share."
- Topics: `hyprland wayland screenshot screen-recorder linux arch-linux qt6 qml cpp`. No homepage.
- Private vulnerability reporting on. Social preview: a 1280×640 still of the Editor with a Frame.

### Hardware pass

- A script runs nested Hyprland with a 1x and a 2x headless output, under `dbus-run-session`, with private `XDG_RUNTIME_DIR` and scratch `HOME`, `DISPLAY` unset, and Tarn on `QT_QUICK_BACKEND=software`, with the daemon started from `hyprland.start`.
- **One exception to `docs/agents/verify.md`, approved by the user for the hardware pass only:** the bench needs the user's session as its host, so one nested window appears on the real desktop. Everything else keeps the rules.
- The pass walks Overlay, Thumbnail, Countdown, Recording bar, Pin and Editor placement at both scales and across both outputs, and times Overlay opening and the Thumbnail at each. Recording stays stubbed there: `gpu-screen-recorder` can't record a nested session.
- It fixes correctness bugs it finds, and adds a test for each at the daemon seam. Performance fixes only if something measures slow; no budgets are set ahead.
- Findings go in `docs/hyprland.md`. Anything too large for this spec becomes an issue in the new repo, and the README's Limits says so.
- Real-iGPU timings on the Radeon 610M are an optional extra check. They do not hold up v1.

## Testing Decisions

A good test checks what's visible from outside: arguments passed to outside programs, socket lines, output and exit codes, colors and positions the user would see, installed files. Never which class did the work. Assertions wait on the settled state, as `docs/agents/verify.md` describes.

Seams, as agreed with the user:

- **The daemon with stub programs (`TestEnv`).** GIF width limit (ffmpeg's filter arguments, MP4 untouched, Trim follows, narrow GIF untouched, `0` off), the CPU fallback flag, the Shell socket path and protocol, `palette_file` (live change, rename-over, missing key, unreadable file, accent order), `accent_color`, `wallpaper_file`, and Countdown placement with and without a reserved top. Prior art: `tests/recording`-style area tests, `tests/island` (moves to `tests/shell`), `tests/trim`, the palette tests.
- **The real `tarn` binary under stub programs.** `tarn doctor`, `tarn --version` and the new error hints: output and exit code. Prior art: `tests/daemon`, which already runs the binary. The `hyprctl` stub learns `binds`, and the `gpu-screen-recorder` stub learns `--info` modes (hardware, CPU only, hang). Doctor's cases: everything present; each required item missing; each optional item missing; no Hyprland; `--info` hanging past its timeout.
- **Install layout (new seam).** `cmake --install` into a scratch folder, asserting every file lands in place. Runs inside `scripts/check.sh`. With tests off (`BUILD_TESTING=OFF`) configure must still succeed.
- **UI tests** for the color popover's label and the Palette swatches and gradients with no Palette file, following `tests/frame_ui` and `tests/backgrounds_ui`.

Checked once and reported, not tests:

- The Waybar script against a stand-in socket, in all four states plus idle and click-to-stop. `socat` isn't in CI, and one example doesn't justify adding it.
- The PKGBUILD in a clean chroot with `namcap`, then `tarn --version`, `tarn doctor`, and that the icons load, in that chroot.
- A one-off scan of the new repo's contents for anything personal or third-party. It is not in the repo, because its patterns hold what it looks for.

Checked by eye on a real monitor: the Countdown under a bar and with no bar; the matugen and `wallpaper_file` setup end to end; a Recording shown by a shell over the Shell socket, and a click on it stopping the Recording; the demo GIF and stills; a Hyprland reload with the copied `tarn.lua`.

## Out of Scope

- Scrolling capture and the Recording Editor, which become the new repo's first two issues.
- Multi-monitor as a feature with its own UI: only correctness fixes.
- Flatpak, Debian, Fedora or Nix packaging; a `tarn-git` package; prebuilt binaries.
- A public post. v1 ends with a pin on the GitHub profile; announcing is a later choice, ideally after someone on different hardware has run Tarn.
- Making Island public.
- A rewrite of history, or GitHub Support involvement ([ADR 0005](../adr/0005-public-v1-is-a-new-repo-from-one-commit.md)).
- Moving the earlier tracker to the new repo.
- Image size budgets, a GIF quality Setting, or another GIF frame rate.

## Further Notes

**Order inside the build.** The socket rename lands with the reference shell's change on the same day, and the author's Tarn settings file gets `palette_file` and `wallpaper_file` before the daemon restarts, or the colors and the current wallpaper disappear. The hardware pass runs after the shell contracts and `tarn doctor`, so it tests the final code. The demo media needs the finished v1 and must exist before the one commit.

**Going public.** v1 went public from one commit, tagged `v1.0.0`; [ADR 0005](../adr/0005-public-v1-is-a-new-repo-from-one-commit.md) has the reasons and the consequences.

**Traps from the docs that apply here.** `hyprctl eval` is the only runtime change; rule patterns must match the whole name; Qt calls Hyprland's pointer a touchpad, so UI tests use `sendWheel`; a blocking `run()` of slow programs in the daemon fails the check.
