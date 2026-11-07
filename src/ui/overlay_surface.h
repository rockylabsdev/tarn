#pragma once
// The layer-shell surface that draws the Overlay: the overlay layer, covering
// the whole monitor (any bar included), exclusive zone -1, exclusive keyboard.
// It shows on Overlay::opened() and hides on closed(). Under a non-Wayland
// platform (the offscreen tests) it is an ordinary window with no layer role.
#include <QObject>
#include <QQuickView>

namespace tarn {

class Overlay;
class Palette;

class OverlaySurface : public QObject {
    Q_OBJECT
public:
    OverlaySurface(Overlay &overlay, Palette &palette, QObject *parent = nullptr);
    QQuickView &view() { return m_view; }
    bool visible() const { return m_view.isVisible(); }

    // Maps the surface once, invisibly and without taking the keyboard, so the
    // first real open does not pay for GPU and scene-graph start-up.
    void warmUp();
    bool warmedUp() const { return m_warmedUp; }

private:
    void show();
    void hide();
    void setInteractive(bool on);

    Overlay &m_overlay;
    QQuickView m_view;
    bool m_warming = false;
    bool m_warmedUp = false;
};

} // namespace tarn
