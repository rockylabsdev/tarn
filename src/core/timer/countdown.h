#pragma once
// The Timer's Countdown. The Overlay (Timer mode) hands it a chosen area; it
// then counts down the Setting `timer_delay`, tells its surface to hide, waits
// until that is off screen, grabs the LIVE screen and hands the shot to the
// Capture store (Thumbnail + History, via Overlay::takeArea).
//
// Time comes only from `Daemon::now()`, so a test moves the clock and calls
// tick() instead of sleeping. A real QTimer calls tick() every 100 ms.
#include <QDateTime>
#include <QObject>
#include <QRect>
#include <QString>
#include <QTimer>

namespace tarn {

class Daemon;

class Countdown : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(int seconds READ seconds NOTIFY changed)
    Q_PROPERTY(QRect area READ area NOTIFY changed)
    Q_PROPERTY(bool fullScreen READ fullScreen NOTIFY changed)
    Q_PROPERTY(QRect monitorRect READ monitorRect NOTIFY changed)
    Q_PROPERTY(QString monitorName READ monitorName NOTIFY changed)
    Q_PROPERTY(int top READ top NOTIFY changed)
public:
    // The pill's distance below the space a bar keeps free at the top of the monitor.
    static constexpr int kBarGap = 12;
    explicit Countdown(Daemon &daemon);

    // Starts counting for `area` (logical px, local to the monitor). Delay 0
    // takes the shot at once. Does nothing while a Countdown runs.
    void start(const QString &monitorName, const QRect &monitorRect, const QRect &area, bool framed = true);
    // Super+Shift+Print again: stops with no capture. False if none was running.
    bool cancel();
    // Moves on to "now": updates the label, and takes the shot when the time is up.
    void tick();

    bool active() const { return m_active; }
    // Whole seconds left, rounded up (3, 2, 1), for the label.
    int seconds() const;
    QRect area() const { return m_area; }
    // The whole monitor is the area (F, or Timer full screen): the surface draws only the pill.
    bool fullScreen() const { return m_area.size() == m_monitorRect.size() && m_area.topLeft() == QPoint(0, 0); }
    QRect monitorRect() const { return m_monitorRect; }
    QString monitorName() const { return m_monitorName; }
    // The pill's top, local to the monitor: Hyprland's reserved top for that monitor, read when
    // the Countdown started (0 if it reports none), plus kBarGap.
    int top() const { return m_top; }

    // Like the Overlay: a surface says it exists, and the shot waits for
    // surfaceGone() after hidden(). With no surface (tests) it does not wait.
    void attachSurface() { m_hasSurface = true; }
    void surfaceGone() { m_closing = false; }

signals:
    void changed();
    void hidden(); // the surface must hide now, before the shot

private:
    void stop();
    void shoot();
    void waitUntilOffScreen(int timeoutMs = 500);

    Daemon &m_daemon;
    QTimer m_ticker;
    bool m_active = false;
    bool m_closing = false;
    bool m_hasSurface = false;
    QDateTime m_deadline;
    int m_shownSeconds = 0;
    int m_top = kBarGap;
    QString m_monitorName;
    QRect m_monitorRect, m_area;
    bool m_framed = true; // false: B skipped the auto-applied Preset
};

} // namespace tarn
