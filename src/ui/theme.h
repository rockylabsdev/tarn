#pragma once
// What every QML screen needs to look like Tarn: the bundled Geist fonts and
// the `theme` context property (the live Palette).
#include <QQmlContext>

namespace tarn {

class Palette;

// Registers the bundled Geist and Geist Mono fonts with Qt, once per process.
void loadFonts();

// Makes the Palette available to a screen's QML as `theme` (theme.accent, ...).
void installTheme(QQmlContext *context, Palette &palette);

} // namespace tarn
