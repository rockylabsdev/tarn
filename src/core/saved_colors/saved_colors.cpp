#include "saved_colors.h"

#include "daemon/daemon.h"
#include "state/remembered_state.h"

namespace tarn {

namespace {
const QString kKey = QStringLiteral("colors.saved");
}

SavedColors::SavedColors(Daemon &daemon) : QObject(), m_state(daemon.service<RememberedState>()) {
    const QStringList parts = m_state.value(kKey).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QColor c(part.trimmed());
        if (c.isValid() && !m_colors.contains(c) && m_colors.size() < kMax)
            m_colors.append(c);
    }
}

void SavedColors::save(const QColor &picked) {
    if (!picked.isValid())
        return;
    const QColor color = QColor::fromRgba(picked.rgba()); // 8 bits a channel, as the file keeps it
    if (!m_colors.isEmpty() && m_colors.first() == color)
        return;
    m_colors.removeAll(color);
    m_colors.prepend(color);
    while (m_colors.size() > kMax)
        m_colors.removeLast();
    store();
}

void SavedColors::remove(int index) {
    if (index < 0 || index >= m_colors.size())
        return;
    m_colors.removeAt(index);
    store();
}

void SavedColors::store() {
    QStringList parts;
    for (const QColor &c : m_colors)
        parts << c.name(QColor::HexArgb);
    if (parts.isEmpty())
        m_state.remove(kKey);
    else
        m_state.setValue(kKey, parts.join(QLatin1Char(',')));
    emit changed();
}

} // namespace tarn
