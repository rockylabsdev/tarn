#include "notifier.h"

#include "daemon/daemon.h"
#include "process/process_runner.h"

namespace tarn {

void Notifier::notify(const QString &summary, const QString &body) {
    QStringList args{QStringLiteral("--app-name=Tarn"), QStringLiteral("--"), summary};
    if (!body.isEmpty())
        args << body;
    // A missing notifier must never break a capture, so the result is ignored.
    m_daemon.service<ProcessRunner>().run(QStringLiteral("notify-send"), args, {}, 3000);
}

} // namespace tarn
