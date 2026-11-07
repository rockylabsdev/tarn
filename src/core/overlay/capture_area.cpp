// `capture area` (Print): opens the Overlay on a frozen image of the monitor
// under the pointer. The capture itself happens when the Selection is confirmed.
#include "daemon/daemon.h"
#include "overlay.h"

namespace tarn {

TARN_MODULE(capture_area, [](Daemon &daemon) {
    daemon.addCommand(QStringLiteral("capture area"),
                      [&daemon](const QStringList &) { return daemon.service<Overlay>().open(OverlayModel::Screenshot); });
});

} // namespace tarn
