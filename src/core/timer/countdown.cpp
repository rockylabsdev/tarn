#include "countdown.h"

#include "daemon/daemon.h"
#include "grab/screen_grabber.h"
#include "hyprland/hyprland_bridge.h"
#include "notify/notifier.h"
#include "overlay/overlay.h"
#include "settings/settings.h"

#include <QEventLoop>
#include <QImage>

namespace tarn {

Countdown::Countdown(Daemon &daemon) : m_daemon(daemon) {
    m_ticker.setInterval(100);
    connect(&m_ticker, &QTimer::timeout, this, &Countdown::tick);
}

int Countdown::seconds() const {
    if (!m_active)
        return 0;
    const qint64 ms = m_daemon.now().msecsTo(m_deadline);
    return int(qMax<qint64>(1, (ms + 999) / 1000));
}

void Countdown::start(const QString &monitorName, const QRect &monitorRect, const QRect &area, bool framed) {
    if (m_active)
        return;
    m_framed = framed;
    m_monitorName = monitorName;
    m_monitorRect = monitorRect;
    m_area = area;
    // One quick `hyprctl monitors` call, like the capture path's; a failed call counts as no bar.
    int reservedTop = 0;
    QString error;
    for (const auto &monitor : m_daemon.service<HyprlandBridge>().monitors(&error))
        if (monitor.name == monitorName)
            reservedTop = qMax(0, monitor.reservedTop);
    m_top = reservedTop + kBarGap;
    const double delay = m_daemon.service<Settings>().timerDelaySeconds();
    m_deadline = m_daemon.now().addMSecs(qRound64(delay * 1000));
    m_active = true;
    m_shownSeconds = seconds();
    emit changed();
    if (delay <= 0) {
        shoot();
        return;
    }
    m_ticker.start();
}

bool Countdown::cancel() {
    if (!m_active)
        return false;
    stop();
    return true;
}

// Ends the Countdown and takes the label away.
void Countdown::stop() {
    m_ticker.stop();
    m_active = false;
    m_closing = m_hasSurface;
    emit changed();
    emit hidden();
}

void Countdown::tick() {
    if (!m_active)
        return;
    if (m_daemon.now() >= m_deadline) {
        shoot();
        return;
    }
    if (const int s = seconds(); s != m_shownSeconds) {
        m_shownSeconds = s;
        emit changed();
    }
}

void Countdown::waitUntilOffScreen(int timeoutMs) {
    if (!m_closing)
        return;
    QEventLoop loop;
    QTimer timeout, poll; // surfaceGone() comes from the surface's own timer
    timeout.setSingleShot(true);
    connect(&poll, &QTimer::timeout, &loop, [&] {
        if (!m_closing)
            loop.quit();
    });
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(5);
    timeout.start(timeoutMs);
    loop.exec();
    m_closing = false;
}

// The time is up: the label goes first (and is off screen), then the live
// screen is grabbed and the area cut out the same way as an Area capture.
void Countdown::shoot() {
    const QString name = m_monitorName;
    const QRect monitorRect = m_monitorRect, area = m_area;
    const bool framed = m_framed;
    stop();
    waitUntilOffScreen();
    const QDateTime captureTime = m_daemon.now();
    QString error;
    const QImage image = QImage::fromData(m_daemon.service<ScreenGrabber>().grabMonitor(name, &error), "PNG");
    if (image.isNull()) {
        if (error.isEmpty())
            error = QStringLiteral("the screenshot could not be read");
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not take the screenshot"), error);
        return;
    }
    m_daemon.service<Overlay>().takeArea(image, monitorRect, area, captureTime, framed);
}

} // namespace tarn
