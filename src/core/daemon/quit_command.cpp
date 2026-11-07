// `tarn quit`: stops the daemon (the scripted way to end an off-screen run).
#include "daemon.h"

#include <QCoreApplication>
#include <QTimer>

namespace tarn {

TARN_MODULE(quit, [](Daemon &daemon) {
    daemon.addCommand(QStringLiteral("quit"), [](const QStringList &) {
        // Later, so the reply to this request is sent first.
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        return Reply::success();
    });
});

} // namespace tarn
