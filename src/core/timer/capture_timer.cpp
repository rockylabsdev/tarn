// `capture timer` (Super+Shift+Print): opens the Overlay on Screenshot with the Timer on (drag an
// area, or F for the full screen), which then starts the Countdown. Pressed
// again during the Countdown, it cancels with no capture.
#include "countdown.h"
#include "daemon/daemon.h"
#include "overlay/overlay.h"

namespace tarn {

TARN_MODULE(capture_timer, [](Daemon &daemon) {
    daemon.addCommand(QStringLiteral("capture timer"), [&daemon](const QStringList &) {
        if (daemon.service<Countdown>().cancel())
            return Reply::success();
        return daemon.service<Overlay>().open(OverlayModel::Screenshot, true);
    });
});

} // namespace tarn
