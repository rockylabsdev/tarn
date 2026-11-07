# History is separate files beside an untouched original, with no index

Each History entry is a group of files that share one name: the original `.png`, which is never rewritten; its Description `.json`, which holds facts about the capture (time, kind, position, saved paths, app and window title, recognized text); and from v2 an optional Project file `.edit.json` for the user's edits. Nothing links the files except the shared name, and there is no folder-wide index. The History window reads every Description when it opens, and expiry and the Trash move the whole group together. The folder stays browsable in a file manager, and a deleted file or a crash can't leave an index out of step with what's on disk. Discarding an edit always gets you back to the real capture.

## Considered Options

- **One `index.json` for the whole folder**: faster to load, but it drifts when files are removed outside Tarn, or when a crash lands between two writes. A month is a few hundred entries, which takes milliseconds to read file by file. If the folder ever opens slowly, a throwaway cache rebuilt from the per-capture files can be added.
- **Edits inside the Description, or a pointer field to them**: one file fewer, but it mixes facts about the capture with the user's work on it. A pointer is also one more thing that can go stale.
- **Keeping the finished, annotated PNG too**: quicker to show, but History would turn into a record of exports and hold two pictures per entry. Saved copies are already listed in the Description's `savedPaths`.

## Consequences

- Fields in a Description are only ever added, never renamed. A missing field means "unknown", so v1 entries are read as they are, with no version number and no migration.
- Recognized text is a `text` field: missing means not yet read, and an empty string means read with no text found.
- Every image capture records app and title. A window capture records its own window, an area capture records the window it overlaps most on the active workspace (from the snapshot taken before the Overlay opens), and a fullscreen capture records the focused window. When no window applies, the fields are left out.
