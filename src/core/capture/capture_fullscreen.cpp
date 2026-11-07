// `capture fullscreen`: the monitor under the pointer becomes a Thumbnail.
#include "daemon/daemon.h"
#include "grab/screen_grabber.h"
#include "history/history.h"
#include "hyprland/hyprland_bridge.h"
#include "notify/notifier.h"
#include "thumbnails/thumbnail_stack.h"

namespace tarn {

static Reply captureFullscreen(Daemon &daemon) {
    const QDateTime captureTime = daemon.now();
    auto &bridge = daemon.service<HyprlandBridge>();
    bridge.takeSnapshot();
    QString error;
    const auto monitor = bridge.monitorUnderPointer(&error);
    QByteArray png;
    if (monitor)
        png = daemon.service<ScreenGrabber>().grabMonitor(monitor->name, &error);
    const auto focused = bridge.focusedWindow();
    const CaptureName name{QStringLiteral("screen"), focused ? focused->app : QString(), focused ? focused->title : QString()};
    const int id = png.isEmpty() ? -1 : daemon.service<ThumbnailStack>().add(png, captureTime, name);
    if (id >= 0) {
        // The untouched original goes to History (in the background).
        const QString historyId = daemon.service<History>().record(
            png, {captureTime, QStringLiteral("fullscreen"), focused ? focused->app : QString(), focused ? focused->title : QString(),
                  monitor->logicalRect});
        daemon.service<ThumbnailStack>().setHistoryId(id, historyId);
        daemon.service<ThumbnailStack>().setOrigin(id, monitor->logicalRect);
    }
    if (id < 0) {
        if (error.isEmpty())
            error = QStringLiteral("the screenshot could not be read");
        daemon.service<Notifier>().notify(QStringLiteral("Tarn could not take the screenshot"), error);
        return Reply::failure(error);
    }
    return Reply::success();
}

TARN_MODULE(capture_fullscreen, [](Daemon &daemon) {
    daemon.addCommand(QStringLiteral("capture fullscreen"), [&daemon](const QStringList &) { return captureFullscreen(daemon); });
});

} // namespace tarn
