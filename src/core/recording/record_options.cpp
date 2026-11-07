#include "record_options.h"

#include "daemon/daemon.h"
#include "hyprland/hyprland_bridge.h"
#include "settings/settings.h"
#include "state/remembered_state.h"

namespace tarn {

RecordOptions::RecordOptions(Daemon &daemon) : m_daemon(daemon) {
    auto &state = daemon.service<RememberedState>();
    m_mic = state.value(QStringLiteral("record.mic"), false).toBool();
    m_system = state.value(QStringLiteral("record.system_audio"), false).toBool();
    m_clicks = state.value(QStringLiteral("record.show_clicks"), false).toBool();
    connect(&daemon.service<Settings>(), &Settings::changed, this, [this](const QString &key) {
        if (key == QLatin1String("show_pointer_in_recordings"))
            emit pointerChanged();
    });
}

void RecordOptions::remember(const char *key, bool on) {
    m_daemon.service<RememberedState>().setValue(QString::fromLatin1(key), on);
    emit changed();
}

void RecordOptions::setMicOn(bool on) {
    if (m_mic != on) {
        m_mic = on;
        remember("record.mic", on);
    }
}

void RecordOptions::setSystemAudio(bool on) {
    if (m_system != on) {
        m_system = on;
        remember("record.system_audio", on);
    }
}

void RecordOptions::setShowClicks(bool on) {
    if (m_clicks != on) {
        m_clicks = on;
        remember("record.show_clicks", on);
    }
}

bool RecordOptions::pointerSetting() const {
    return m_daemon.service<Settings>().showPointerInRecordings();
}

void RecordOptions::refreshPointer() {
    const bool software = m_daemon.service<HyprlandBridge>().softwarePointer();
    if (software != m_software) {
        m_software = software;
        emit pointerChanged();
    }
}

bool RecordOptions::pointerShown() {
    refreshPointer();
    return m_software || pointerSetting();
}

} // namespace tarn
