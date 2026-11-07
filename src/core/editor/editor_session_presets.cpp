// The Presets part of EditorSession: what the Background panel's Presets
// section calls. The file itself is PresetStore's.
#include "editor_session.h"

#include "daemon/daemon.h"
#include "editor.h"
#include "preset/preset_store.h"

namespace tarn {

QVariantList EditorSession::presets() const {
    auto &store = m_editor.daemon().service<PresetStore>();
    const QString marked = store.autoName();
    QVariantList list;
    for (const QString &name : store.names())
        list << QVariantMap{{QStringLiteral("name"), name},
                            {QStringLiteral("auto"), name == marked},
                            {QStringLiteral("missing"), store.isMissing(name)}};
    return list;
}

bool EditorSession::savePreset(const QString &name) {
    if (!m_document->frame().active() && !m_document->frame().watermark.on())
        return false;
    const bool ok = m_editor.daemon().service<PresetStore>().save(name, m_document->frame());
    emit backgroundsChanged();
    return ok;
}

bool EditorSession::applyPreset(const QString &name) {
    if (m_mode != Mode::None)
        return false;
    const auto frame = m_editor.daemon().service<PresetStore>().resolved(name, true);
    if (!frame)
        return false;
    m_document->setFrame(*frame, QStringLiteral("Preset"));
    frameChanged();
    return true;
}

bool EditorSession::deletePreset(const QString &name) {
    const bool ok = m_editor.daemon().service<PresetStore>().remove(name);
    emit backgroundsChanged();
    return ok;
}

bool EditorSession::setAutoPreset(const QString &name) {
    const bool ok = m_editor.daemon().service<PresetStore>().setAuto(name);
    emit backgroundsChanged();
    return ok;
}

} // namespace tarn
