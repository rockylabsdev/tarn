#include "click_ripples.h"

#include "daemon/daemon.h"
#include "hyprland/hyprland_bridge.h"
#include "notify/notifier.h"
#include "record_options.h"
#include "recording_session.h"
#include "state/remembered_state.h"

namespace tarn {

namespace {
const QString kBoundKey = QStringLiteral("record.clicks_bound");
const QString kCommand = QStringLiteral("tarn record click");
} // namespace

ClickRipples::ClickRipples(Daemon &daemon) : m_daemon(daemon), m_session(daemon.service<RecordingSession>()) {
    // A crash (or a kill) while the binds were in leaves them in Hyprland: take them out now.
    auto &state = daemon.service<RememberedState>();
    if (state.value(kBoundKey, false).toBool()) {
        QString error;
        if (daemon.service<HyprlandBridge>().removeClickBinds(&error))
            state.setValue(kBoundKey, false);
    }
    connect(&m_session, &RecordingSession::stateChanged, this, &ClickRipples::onState);
    connect(&m_session, &RecordingSession::barVisibleChanged, this, &ClickRipples::onState);
}

void ClickRipples::onState() {
    const auto s = m_session.state();
    const bool live = (s == RecordingSession::Starting || s == RecordingSession::Recording || s == RecordingSession::Paused) && !m_session.stopping();
    if (!live) {
        unbind();
        return;
    }
    // Show clicks is read when a Recording (re)starts, not while it runs.
    if (s == RecordingSession::Starting) {
        const bool wanted = m_daemon.service<RecordOptions>().showClicks();
        if (wanted && !m_bound)
            bind();
        else if (!wanted)
            unbind();
    }
}

void ClickRipples::bind() {
    // Written first: if the daemon dies between the two, the next start still cleans up.
    m_daemon.service<RememberedState>().setValue(kBoundKey, true);
    QString error;
    if (!m_daemon.service<HyprlandBridge>().addClickBinds(kCommand, &error)) {
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn cannot show clicks"), error);
        unbind();
        return;
    }
    m_bound = true;
    emit activeChanged();
}

void ClickRipples::unbind() {
    auto &state = m_daemon.service<RememberedState>();
    if (!m_bound && !state.value(kBoundKey, false).toBool())
        return;
    QString error;
    if (m_daemon.service<HyprlandBridge>().removeClickBinds(&error))
        state.setValue(kBoundKey, false);
    if (m_bound) {
        m_bound = false;
        emit activeChanged();
    }
}

bool ClickRipples::click(const QString &) {
    if (!m_bound || m_session.state() != RecordingSession::Recording)
        return false;
    QString error;
    const auto pointer = m_daemon.service<HyprlandBridge>().pointerPosition(&error);
    if (!pointer)
        return false;
    // Layout position minus the monitor's origin = logical px local to the monitor, like the area.
    const QPointF local = *pointer - QPointF(m_session.monitorOrigin());
    if (!QRectF(m_session.area()).contains(local))
        return false;
    emit rippled(local);
    return true;
}

TARN_MODULE(click_ripples, [](Daemon &daemon) {
    daemon.service<ClickRipples>(); // also removes binds a crash left behind
    daemon.addCommand(QStringLiteral("record click"), [&daemon](const QStringList &args) {
        daemon.service<ClickRipples>().click(args.value(0));
        return Reply::success();
    });
});

} // namespace tarn
