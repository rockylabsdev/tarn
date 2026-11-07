#pragma once
// The Project file (GLOSSARY: Project file): the edits made to one capture,
// saved as JSON beside its untouched History original (<id>.edit.json) so the
// capture reopens in the Editor with every Annotation still movable.
//
//   { "annotations": [ { "id": 3, "kind": "rect", "data": {...} }, ... ],  bottom first
//     "crop":  { "rect": {x,y,w,h} | null, "quarterTurns", "mirrored", "scale" },
//     "frame": { Frame::toMap() without its derived keys, locked to its look } }
//
// It never holds undo history or view state: a loaded document starts with empty
// undo (the Editor opens it at Fit, on Rectangle).
//
// Extending it (Callouts, Bend, Watermark): fields are only ever added and a
// missing field means its default, so older files keep loading. A new Annotation
// kind adds Annotation::toJson plus a RegisterAnnotationLoader; a new Frame field
// rides along with Frame::toMap / fromMap; a new top-level part (if ever needed)
// is one more key in projectToJson / loadProject.
//
// "Locked to its look": a Frame that follows the desktop (a Palette gradient, the
// current wallpaper) is saved as the exact colors and the actual file path it had.
#include "capture_document.h"

#include <QByteArray>
#include <QString>

namespace tarn {

// The edits of `document` as JSON bytes. Annotations of a kind without a loader are left out.
QByteArray projectToJson(const CaptureDocument &document);

// The words a Project file holds: every Annotation's `text` (Text, Callouts), one per line,
// in drawing order. Empty for an unreadable file. What the History search reads, so it
// never needs to know the file's layout.
QString projectText(const QByteArray &json);

struct ProjectLoad {
    bool ok = false;
    QString error; // when not ok
    // The Frame's Background pointed at a file that is gone (a deleted wallpaper); it
    // was replaced by a blurred copy of the shot. `missingFile` names it.
    bool backgroundMissing = false;
    QString missingFile;
};

// Rebuilds the edits into `document` (a fresh document on the original shot). Untouched
// when the bytes are not a readable Project file.
ProjectLoad loadProject(CaptureDocument &document, const QByteArray &json);

} // namespace tarn
