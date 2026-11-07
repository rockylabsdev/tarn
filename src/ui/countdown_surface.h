#pragma once
// The Timer's Countdown surface: one full-monitor layer surface (Overlay layer) that draws the
// red pill at the top center and, for an area, the dim and outline round it. It never takes the
// keyboard or the pointer (input-transparent over the whole monitor, so clicks reach the windows
// beneath and an open menu stays open) and hides on Countdown::hidden(), before the shot. Under a
// non-Wayland platform (the offscreen tests) it is an ordinary window.
#include <QObject>
#include <QQuickView>

namespace tarn {

class Countdown;
class Palette;

class CountdownSurface : public QObject {
    Q_OBJECT
public:
    CountdownSurface(Countdown &countdown, Palette &palette, QObject *parent = nullptr);
    QQuickView &view() { return m_view; }
    bool visible() const { return m_view.isVisible(); }

    // The pill's size: Style's countdown* values (C++ cannot read the QML singleton).
    static constexpr int kWidth = 104;  // Style.countdownWidth
    static constexpr int kHeight = 44;  // Style.countdownHeight
    // The pill within a monitor of this logical size (the surface covers the whole monitor), `top`
    // px down: Countdown::top(), the reserved top plus a gap.
    static QRect pillRect(const QSize &monitor, int top);

private:
    void show();
    void hide();

    Countdown &m_countdown;
    QQuickView m_view;
};

} // namespace tarn
