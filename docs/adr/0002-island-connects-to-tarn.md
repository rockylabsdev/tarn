# Island connects to Tarn's socket; a Recording always takes Island's circle

> [ADR 0004](0004-shell-contracts-are-files-and-a-socket.md) makes this shell-neutral: the socket becomes the Shell socket (`shell.sock`), its protocol unchanged, and Island is one client of it.

While a Recording runs, Island shows it, and the two programs talk over a Unix socket that the `tarn daemon` creates (`$XDG_RUNTIME_DIR/tarn/shell.sock`). Island connects to it, Tarn sends a JSON line on every change, and Island sends back only "stop". The open connection is how each side knows the other is running: no polling, nothing stale after a crash, and an Island restart gets the current state the moment it reconnects. Tarn's own Recording bar shows either way. Island adds an at-a-glance circle with one-click stop, the way a menu-bar icon and a floating bar can both show the same state. The protocol is in [docs/shell-socket.md](../shell-socket.md).

## Considered Options

- **Tarn calls Island with `qs ipc`** (how Island's other integrations work, and how its keybinds call it): Tarn would have to keep checking whether Island is running, and an Island restart would lose a recording's state until Tarn sent it again.
- **D-Bus**: the standard Linux route, but Quickshell has no general D-Bus client, so Island would have to run `gdbus monitor` and parse its output.
- **A state file Island watches**: a crash leaves it stale, and Tarn can't tell whether anything is showing the Recording.
- **Island holds all the controls** (an expansion with pause, restart, delete): tidier on screen, but Tarn needs its bar anyway when Island isn't running, so it would build the same controls twice.

## Consequences

- Island's rule bends: a Recording always takes the **Circle**, and rank decides the pill among everything else (iOS does the same). Island's glossary entries for Circle and Rank change with it, in the Island repo.
- The recording dot is a fixed red (`#FF453A`, iOS's dark-mode red) in both programs, whatever the wallpaper. Island's wallpaper-derived `recording` accent isn't red and didn't read as "recording".
- Tarn owns this contract. The Island side is tracked in Island's own repo. Until it ships, Tarn behaves as if Island isn't running.
