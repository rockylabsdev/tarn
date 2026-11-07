# The History window

Read when working on the History store's events, the History window, its model or `tarn history`.

- `History` (the store) emits `entryAdded` / `entryChanged` / `entryRemoved` with the Description; the sweep, Delete (`trash`) and `restore` all emit them. `History::flush()` also delivers the queued add/change events.
- `HistoryWindow` service (`src/core/history_window`, command `history`) and its `HistoryModel`, the list the QML grid binds to: flat rows, a day heading then that day's captures, newest first. Tests (`tests/history_window`) read rows through the model's roles (`HeadingRole`, `DayRole`, `IdRole`, ...) and drive selection with `move("left|right|up|down", columns)`. Plant entries by writing `<id>.png` + `<id>.json` into `env.scratch() + "/data/tarn/history"` before `open()` (the model reads the disk only then).
- `tarn history` asks Hyprland for `activewindow -j` (stub fixture `activewindow.json`; set `{"title":"Tarn history"}` to model it focused) and brings the window forward with a logged `hyprctl eval` focus call.
- `tests/history_window_ui` drives `HistoryWindows` / `History.qml` off-screen. Needs-eyes: the opening size (1200 x 780) and tiles across on the real monitor, float/center/no animation, Alt+Print by key code (`wev`) and the real focus call.
