#pragma once
// Show clicks. While a Recording that has Show clicks on is starting,
// running or paused, Hyprland carries three non-consuming mouse binds that run
// the internal command `tarn record click <left|middle|right>`. Each notice makes
// the daemon read the pointer; a press inside the recorded area emits `rippled`,
// which the Ripple surface draws. The binds go the moment a stop or delete is
// asked for, or the Recording fails. A crash leaves them behind, so state.toml
// remembers `[record] clicks_bound` and the next daemon start removes them.
//
// Show clicks is read at each `Starting` (so a restart re-reads it).
#include <QObject>
#include <QPointF>
#include <QSize>

namespace tarn {

class Daemon;
class RecordingSession;

class ClickRipples : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)   // binds are in: the Ripple surface shows
public:
    explicit ClickRipples(Daemon &daemon);

    bool active() const { return m_bound; }
    // The notice from a bind. Draws only while Recording, and only inside the area.
    // Returns true if a Ripple was emitted.
    bool click(const QString &button);

signals:
    void activeChanged();
    // Where, in logical px local to the recorded monitor (what the Ripple surface uses).
    void rippled(const QPointF &position);

private:
    void onState();
    void bind();
    void unbind();

    Daemon &m_daemon;
    RecordingSession &m_session;
    bool m_bound = false;
};

} // namespace tarn
