#pragma once
// The Show clicks layer: a click-through layer-shell surface over the recorded
// monitor, on screen only while ClickRipples is active (a Recording with Show
// clicks on). It draws Ripples (Ripples.qml) and takes neither pointer nor keyboard,
// so clicks reach the app underneath. Scope "tarn-ripples" gets the no-animation rule.
#include <QObject>
#include <QQuickView>

namespace tarn {

class ClickRipples;
class Palette;
class RecordingSession;

class RippleSurface : public QObject {
    Q_OBJECT
public:
    RippleSurface(ClickRipples &ripples, RecordingSession &session, Palette &palette, QObject *parent = nullptr);
    QQuickView &view() { return m_view; }
    bool visible() const { return m_view.isVisible(); }

private:
    void sync();

    ClickRipples &m_ripples;
    RecordingSession &m_session;
    QQuickView m_view;
};

} // namespace tarn
