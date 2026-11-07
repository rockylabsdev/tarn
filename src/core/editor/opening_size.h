#pragma once
// The Editor window's size when it first appears. Hyprland ignores a window's
// own resizes after it is on screen, so the size has to be right at first show,
// and it is computed from the monitor, never hard-coded.
#include <QSize>

namespace tarn {

class Daemon;

// The monitor an Editor or Trim window will open on: its logical size and scale.
struct MonitorSize {
    QSize size{1920, 1080};
    double scale = 1.0;
};
// The monitor under the pointer; else the primary screen; else 1920x1080 at scale 1.
MonitorSize monitorSizeUnderPointer(Daemon &daemon);

struct EditorMetrics {
    static constexpr int barHeight = 52;  // the top toolbar and the bottom bar
    static constexpr int canvasMargin = 40; // clear space round the shot
    static constexpr int minWidth = 1180; // what the toolbar needs
};

// The shot at 100% (its pixels over the monitor scale gives logical pixels) plus
// both bars and a 40 px margin on each side, clamped between half the monitor's
// width x 56% of its height and two-thirds x 70%, and never narrower than 1180.
// `monitor` is the monitor's size in logical pixels.
QSize editorOpeningSize(const QSize &shotPixels, const QSize &monitor, double scale = 1.0);

} // namespace tarn
