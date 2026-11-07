// `capture record` (Ctrl+Print): opens the Overlay in record mode (area only).
// While a Recording is running, the same key stops it. At start-up the daemon
// also saves whatever a crash left in the kept folder.
#include "daemon/daemon.h"
#include "overlay/overlay.h"
#include "recording_session.h"
#include "recording_store.h"

namespace tarn {

TARN_MODULE(capture_record, [](Daemon &daemon) {
    daemon.service<RecordingStore>().recover();
    daemon.addCommand(QStringLiteral("capture record"), [&daemon](const QStringList &) {
        auto &session = daemon.service<RecordingSession>();
        if (session.active()) {
            session.stop();
            return Reply::success();
        }
        return daemon.service<Overlay>().open(OverlayModel::Record);
    });
});

} // namespace tarn
