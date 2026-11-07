// `tarn pin lock-toggle` (Super+Shift+P): lock every Pin if any is unlocked,
// unlock them all when all are locked. A Locked Pin can't be focused, so this is
// the only way back.
#include "daemon/daemon.h"
#include "pin/pins.h"

namespace tarn {

TARN_MODULE(pin_lock, [](Daemon &daemon) {
    daemon.addCommand(QStringLiteral("pin lock-toggle"), [&daemon](const QStringList &) {
        return daemon.service<Pins>().lockToggle() ? Reply::success() : Reply::failure(QStringLiteral("a Pin could not be locked or unlocked"));
    });
});

} // namespace tarn
