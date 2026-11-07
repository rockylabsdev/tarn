#include "screen_grabber.h"

#include "daemon/daemon.h"
#include "overlay/overlay.h"
#include "process/process_runner.h"

namespace tarn {

QByteArray ScreenGrabber::grabMonitor(const QString &monitorName, QString *error) {
    // A closing Overlay must be off screen first, or grim would capture it.
    m_daemon.service<Overlay>().waitUntilOffScreen();
    // "-" sends the PNG to stdout. No -c: grim's pointer default is "left out".
    const auto result = m_daemon.service<ProcessRunner>().run(
        QStringLiteral("grim"), {QStringLiteral("-o"), monitorName, QStringLiteral("-t"), QStringLiteral("png"), QStringLiteral("-")},
        {}, 10000);
    if (!result.succeeded()) {
        *error = result.describe(QStringLiteral("grim"));
        return {};
    }
    if (!result.out.startsWith("\x89PNG")) {
        *error = QStringLiteral("grim did not produce a PNG");
        return {};
    }
    return result.out;
}

} // namespace tarn
