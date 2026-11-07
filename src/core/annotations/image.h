#pragma once
// An added image: a picture placed on the shot as an Annotation. Not a
// drawing tool (no registry entry, no key): the Editor adds one from a file, a
// drop or the clipboard. It is movable and resizable like any Box, and where it
// reaches past the shot the canvas grows.
#include "document/annotation.h"

namespace tarn {

std::unique_ptr<Annotation> makeImageAnnotation(const QImage &image, const QRectF &rect);

} // namespace tarn
