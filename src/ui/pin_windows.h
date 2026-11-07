#pragma once
// The Pins' windows. One ordinary frameless top-level window per Pin, titled
// "Tarn pin <id>" (Pins::title). The static window rule in tarn.lua matches that
// title and floats, pins and un-animates it; where it opens is a per-window rule
// the Pins service has already added, so the size set here is the same one.
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQuickView>

namespace tarn {

class Palette;
class Pins;

// What the QML window calls on its own window (context property `win`).
class PinWindow : public QObject {
    Q_OBJECT
public:
    PinWindow(Pins &pins, Palette &palette, int pin, QObject *parent = nullptr);
    QQuickView &view() { return m_view; }
    // Dragging the Pin: the compositor moves the window.
    Q_INVOKABLE bool startMove() { return m_view.startSystemMove(); }

private:
    QQuickView m_view;
};

class PinWindows : public QObject {
    Q_OBJECT
public:
    PinWindows(Pins &pins, Palette &palette, QObject *parent = nullptr);
    // The window of an open Pin, or null.
    QQuickView *viewFor(int pin) const;

private:
    void onOpened(int pin);

    Pins &m_pins;
    Palette &m_palette;
    QHash<int, QPointer<PinWindow>> m_windows;
};

} // namespace tarn
