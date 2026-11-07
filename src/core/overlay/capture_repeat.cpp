// `capture repeat` (Shift+Print): captures the focused app's Last area again with
// no Overlay, straight to a Thumbnail. With no Last area for that app (nothing
// taken over it yet, state file deleted, no focused window, or the area no longer
// lies on a monitor) it opens the Overlay instead.
#include "daemon/daemon.h"
#include "grab/screen_grabber.h"
#include "hyprland/hyprland_bridge.h"
#include "last_area.h"
#include "notify/notifier.h"
#include "overlay.h"
#include "state/remembered_state.h"

namespace tarn {

static Reply captureRepeat(Daemon &daemon) {
    const QDateTime captureTime = daemon.now();
    auto &overlay = daemon.service<Overlay>();
    auto &bridge = daemon.service<HyprlandBridge>();
    bridge.takeSnapshot();
    const auto focused = bridge.focusedWindow();
    const QRect area = focused ? lastarea::recall(daemon.service<RememberedState>(), focused->app) : QRect();
    if (area.isEmpty())
        return overlay.open(OverlayModel::Screenshot);

    QString error;
    const auto monitor = bridge.monitorContaining(area.center(), &error);
    if (!monitor)
        return overlay.open(OverlayModel::Screenshot); // the area is no longer on a monitor
    QImage image = QImage::fromData(daemon.service<ScreenGrabber>().grabMonitor(monitor->name, &error), "PNG");
    if (image.isNull()) {
        if (error.isEmpty())
            error = QStringLiteral("the screenshot could not be read");
        daemon.service<Notifier>().notify(QStringLiteral("Tarn could not take the screenshot"), error);
        return Reply::failure(error);
    }
    const QRect local = area.translated(-monitor->logicalRect.topLeft());
    overlay.takeArea(image, monitor->logicalRect, local.intersected(QRect(QPoint(0, 0), monitor->logicalRect.size())), captureTime);
    return Reply::success();
}

TARN_MODULE(capture_repeat, [](Daemon &daemon) {
    daemon.addCommand(QStringLiteral("capture repeat"), [&daemon](const QStringList &) { return captureRepeat(daemon); });
});

} // namespace tarn
