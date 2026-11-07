#include "last_area.h"

#include "state/remembered_state.h"

#include <QStringList>
#include <QUrl>

namespace tarn::lastarea {

static QString keyFor(const QString &app) {
    // Percent-encoded so a class with dots or odd characters stays one flat key.
    return QStringLiteral("last_areas.") + QString::fromLatin1(QUrl::toPercentEncoding(app, {}, ".~"));
}

void remember(RememberedState &state, const QString &app, const QRect &r) {
    if (app.isEmpty() || r.isEmpty())
        return;
    state.setValue(keyFor(app), QStringLiteral("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
}

QRect recall(const RememberedState &state, const QString &app) {
    if (app.isEmpty())
        return {};
    const QStringList p = state.value(keyFor(app)).toString().split(',');
    if (p.size() != 4)
        return {};
    int n[4];
    for (int i = 0; i < 4; ++i) {
        bool ok = false;
        n[i] = p[i].toInt(&ok);
        if (!ok)
            return {};
    }
    return QRect(n[0], n[1], n[2], n[3]);
}

} // namespace tarn::lastarea
