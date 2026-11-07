# The Project file

Read when working on saving or reopening an edited capture (`*.edit.json`).

- `src/core/document/project_file.*`: `projectToJson(document)` and `loadProject(document, bytes)`. A kind saves itself with `Annotation::toJson()` and registers a loader with `RegisterAnnotationLoader` next to its `RegisterAnnotationKind` (an added image has a loader but no kind). Fields are only ever added; a missing field means its default. A Frame field rides along with `Frame::toMap()` / `fromMap()`, so a new Frame part (the Watermark) needs no change in the Project file code except locking anything that follows the desktop (`lockedLook`).
- `History` owns the file: `saveProject(id, document)` (writes, or deletes when nothing is edited; trashes a damaged one first), `loadProject(id, original)` (notifies for a damaged file or a missing wallpaper), `revertProject(id)`, and `trash` / `restore` / `sweep` move it with its entry. `ids()` skips `*.edit.json`.
- Writers: the Editor's keeping closes (`EditorSession::writeProject`) and `ThumbnailStack::setHistoryId` (the auto-applied Preset). The Editor reads it in `Editor::open` when the Thumbnail has no committed document.
- `tests/project` drives all of it through `EditorSession` and reads the files, the Trash and `notify-send` calls. Pixelate in a round-trip test must be "black out" (other styles draw fresh noise on every export).
