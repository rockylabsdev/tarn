# Tarn is its own Qt6 program: C++ underneath, QML on screen

Tarn is built from scratch in Qt6, with C++ for the core and QML (QtQuick) for every screen, LayerShellQt for the full-screen overlay, and CMake to build it. It reads Omasnap (MIT, Qt6 C++, the nearest whole-flow tool on Hyprland) and Satty (MPL-2.0, the best annotation editor) for how they solve things and may copy small pieces of either, but forks neither and copies no GPL code. Qt's QML matches Island's look, compiles the pixelate shader in a normal build step, gives drag-out and clipboard directly in C++, and doesn't make anyone install Quickshell. Omasnap already shows Qt6 with LayerShellQt working on Hyprland for overlay, drag-out and pin.

## Considered Options

- **Quickshell/QML** (what Island uses): the closest match to Island and the most built-in help for the overlay, but it is pre-1.0 and breaks between releases, has no build step to compile the pixelate shader, has unproven drag-out to other apps, and would be a dependency for anyone installing Tarn.
- **Rust + GTK4** (Satty's stack): the strongest ready-made editor parts, but it is the hardest way to match Island's look, because libadwaita has its own widget style, and Rust would be a new toolchain.
- **Forking Omasnap**: most of the flow on day one, but it changes weekly, uses Qt Widgets with Omarchy's theme rather than Island's look, and is a large codebase written by agents that hasn't been run on this machine.

## Consequences

- grim takes the screenshot first, and Tarn's own overlay picks the area on the frozen image; `slurp` is not used. Saved images come from grim's full-resolution file.
- One `tarn` program: `tarn daemon` runs from login so UI opens instantly, and short commands (`tarn capture area`, ...) bound to keys pass requests to it and exit.
- Colors come live from Island's `palette.json` with a built-in fallback; Tarn keeps its own copy of Island's style values and its own controls, modeled on Island's.
- Tarn is MIT-licensed, and GPL programs (`wl-copy`, `tesseract`, `gpu-screen-recorder`) are run as separate commands, never linked or copied.
