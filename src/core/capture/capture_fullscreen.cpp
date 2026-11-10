// `capture fullscreen`: the monitor under the pointer becomes a Thumbnail.
#include "daemon/daemon.h"
#include "grab/screen_grabber.h"
#include "history/history.h"
#include "hyprland/hyprland_bridge.h"
#include "notify/notifier.h"
#include "storage/png_encoder.h"
#include "thumbnails/thumbnail_stack.h"

namespace tarn {

static Reply captureFullscreen(Daemon &daemon) {
    if (daemon.service<ScreenGrabber>().busy())
        return Reply::success(); // pressed again while a shot is being taken
    const QDateTime captureTime = daemon.now();
    auto &bridge = daemon.service<HyprlandBridge>();
    bridge.takeSnapshot();
    QString error;
    const auto monitor = bridge.monitorUnderPointer(&error);
    // Off screen before grim, back with the new Thumbnail once it is on the stack.
    CaptureHide hide = daemon.service<ThumbnailStack>().hideForCapture();
    QImage shot;
    if (monitor)
        shot = daemon.service<ScreenGrabber>().grabMonitor(monitor->name, &error);
    const auto focused = bridge.focusedWindow();
    const CaptureName name{QStringLiteral("screen"), focused ? focused->app : QString(), focused ? focused->title : QString()};
    // The card shows the pixels at once; the PNG is made off the daemon's thread.
    const QFuture<QByteArray> png = shot.isNull() ? QFuture<QByteArray>() : encodePngInBackground(shot);
    const int id = shot.isNull() ? -1 : daemon.service<ThumbnailStack>().add(shot, png, captureTime, name);
    if (id >= 0) {
        // The untouched original goes to History (in the background, once its PNG is made).
        const QString historyId = daemon.service<History>().record(
            png, {captureTime, QStringLiteral("fullscreen"), focused ? focused->app : QString(), focused ? focused->title : QString(),
                  monitor->logicalRect});
        daemon.service<ThumbnailStack>().setHistoryId(id, historyId);
        daemon.service<ThumbnailStack>().setOrigin(id, monitor->logicalRect);
    }
    hide.reset();
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
