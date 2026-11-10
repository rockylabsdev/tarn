#pragma once
// The layer-shell surface in the bottom-left corner that shows the Thumbnail
// stack. It never takes keyboard focus, and it is on screen only while the
// stack is not empty and no Capture hide is on (then it goes at once, with no
// exit motion: anything still on screen would be in the shot). Under a non-Wayland platform (the offscreen tests) it is
// an ordinary hidden window with no layer-shell role.
#include <QObject>
#include <QQuickView>

namespace tarn {

class Palette;
class ThumbnailStack;

class ThumbnailSurface : public QObject {
    Q_OBJECT
public:
    // With the daemon's live Palette; the one-argument form reads the Palette file itself.
    ThumbnailSurface(ThumbnailStack &stack, Palette &palette, QObject *parent = nullptr);
    explicit ThumbnailSurface(ThumbnailStack &stack, QObject *parent = nullptr);
    QQuickView &view() { return m_view; }
    bool visible() const { return m_view.isVisible(); }

private slots:
    void sync();
    void follow();
    void updateMask();

private:
    void applyScreen();

    ThumbnailStack &m_stack;
    QQuickView m_view;
};

} // namespace tarn
