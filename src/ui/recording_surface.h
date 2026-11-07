#pragma once
// The layer-shell surface that shows a running Recording: the area's red
// outline and the Recording bar (Recording.qml). It covers the monitor but only
// the bar takes the pointer, and it never takes the keyboard or focus, so the
// recorded app keeps both (Esc reaches it, never this). Shown from `starting` to
// `encoding`; the scope starts with "tarn" so the no-animation layer rule applies.
#include <QObject>
#include <QQuickView>

namespace tarn {

class Palette;
class RecordingSession;

class RecordingSurface : public QObject {
    Q_OBJECT
public:
    RecordingSurface(RecordingSession &session, Palette &palette, QObject *parent = nullptr);
    QQuickView &view() { return m_view; }
    bool visible() const { return m_view.isVisible(); }

private:
    void sync();
    void updateMask();

    RecordingSession &m_session;
    QQuickView m_view;
};

} // namespace tarn
