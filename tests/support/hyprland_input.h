#pragma once
// Input the way Hyprland delivers it. Qt reports Hyprland's seat pointer as a
// touchpad, even for a mouse wheel (docs/hyprland.md), and a QWheelEvent built
// without a device comes from a mouse, which a WheelHandler accepts by default.
// A test built that way passes while the desktop ignores every scroll.
#include <QCoreApplication>
#include <QPoint>
#include <QPointingDevice>
#include <QWheelEvent>
#include <QWindow>

namespace tarn::test {

// The pointer device Qt reports on Hyprland: a touchpad named after the seat.
inline const QPointingDevice *hyprlandPointer() {
    static const QPointingDevice seat(QStringLiteral("touchpad"), 8589934592, QInputDevice::DeviceType::TouchPad,
                                      QPointingDevice::PointerType::Finger,
                                      QInputDevice::Capability::Position | QInputDevice::Capability::Scroll, 10, 0,
                                      QStringLiteral("Hyprland"));
    return &seat;
}

// One wheel notch is dy = 120 (positive scrolls up / zooms in), at `at` in window coordinates.
inline void sendWheel(QWindow *window, QPoint at, int dy, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QWheelEvent e(at, window->mapToGlobal(at), QPoint(0, dy / 8), QPoint(0, dy), Qt::NoButton, mods, Qt::NoScrollPhase,
                  false, Qt::MouseEventNotSynthesized, hyprlandPointer());
    QCoreApplication::sendEvent(window, &e);
}

} // namespace tarn::test
