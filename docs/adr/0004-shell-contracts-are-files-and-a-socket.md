# Any shell plugs in through two files and a socket; Island is one client

Tarn takes its Palette from a JSON file and the current wallpaper from a one-line text file, both named by Settings (`palette_file`, `wallpaper_file`, both empty by default). It reports a running Recording on the Shell socket (`$XDG_RUNTIME_DIR/tarn/shell.sock`, the protocol [ADR 0002](0002-island-connects-to-tarn.md) chose for Island, unchanged). The split follows the kind of data. Colors and the wallpaper are state that outlives any process, and every tool can already write a file: matugen through a template, a wallpaper script with one `echo`. A Recording is a live event, and an open connection also tells each side the other is running. Nothing in Tarn names Island. Island, the author's Quickshell shell, is the reference client, and a Waybar module is the documented second one.

## Considered Options

- **Detect each tool** (find matugen's output, run `swww query`, read hyprpaper's config): one integration per tool, broken whenever one of them changes, and nothing for a setup Tarn doesn't know about.
- **Tarn derives its own palette from the wallpaper**: only the wallpaper path would be needed, but the colors wouldn't match the shell's. matugen, Noctalia and Island each pick a different scheme from the same image, and matching the desktop is the point of the Palette.
- **The shell pushes colors and wallpaper over the socket**: one channel, but it works only while a shell program is running and connected. Waybar plus a matugen template has no such program, and the colors would be lost on every restart.
- **Follow the Qt theme** (`QPalette` from qt6ct or Kvantum): no setup for desktops that already theme Qt apps, but QPalette has fewer colors than Tarn's surface steps, has no matching gradient colors, and qt6ct doesn't update running apps live.
- **Accent from the XDG desktop portal** (`org.freedesktop.appearance.accent-color`): a real standard, but it gives only an accent, and Hyprland's portal doesn't provide it.

## Consequences

- The palette file holds Material 3 color names as matugen writes them (`surface_container_low`, `primary`, ...). It may also hold `accent` and `on_accent`, which win over `primary` and `on_primary`; Island writes them for its contrast-checked accent. An `accent_color` Setting overrides all of it with a fixed color.
- With no wallpaper file, the Background panel offers only the images in `wallpaper_folder`.
- The Countdown sits 12 px below whatever space the monitor reserves at its top (Hyprland's `reserved`), so it clears Island, Waybar or no bar without a Setting.
- A user must point two Settings at files. The README's matugen and wallpaper snippets cover that.
