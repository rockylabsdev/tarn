#include "screen_grabber.h"

#include "daemon/daemon.h"
#include "overlay/overlay.h"
#include "process/process_runner.h"
#include "thumbnails/thumbnail_stack.h"

#include <QScopeGuard>

namespace tarn {

QImage ScreenGrabber::grabMonitor(const QString &monitorName, QString *error) {
    m_busy = true;
    const auto done = qScopeGuard([this] { m_busy = false; });
    // A closing Overlay must be off screen first, or grim would capture it.
    m_daemon.service<Overlay>().waitUntilOffScreen();
    // So must the Thumbnails, which the caller has hidden (ThumbnailStack::hideForCapture).
    m_daemon.service<ThumbnailStack>().waitUntilOffScreen();
    // "-" sends the picture to stdout. No -c: grim's pointer default is "left out".
    const auto result = m_daemon.service<ProcessRunner>().run(
        QStringLiteral("grim"), {QStringLiteral("-o"), monitorName, QStringLiteral("-t"), QStringLiteral("ppm"), QStringLiteral("-")},
        {}, 10000);
    if (!result.succeeded()) {
        *error = result.describe(QStringLiteral("grim"));
        return {};
    }
    QImage image = result.out.startsWith("P6") ? QImage::fromData(result.out, "PPM") : QImage();
    if (image.isNull())
        *error = QStringLiteral("grim did not produce a picture");
    return image;
}

} // namespace tarn
