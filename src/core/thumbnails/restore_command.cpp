// `restore` (Super+Ctrl+Print): brings back the last closed Thumbnail.
#include "daemon/daemon.h"
#include "thumbnails/thumbnail_stack.h"

namespace tarn {

TARN_MODULE(restore, [](Daemon &daemon) {
    daemon.addCommand(QStringLiteral("restore"), [&daemon](const QStringList &) {
        auto &stack = daemon.service<ThumbnailStack>();
        if (!stack.canRestore())
            return Reply::failure(QStringLiteral("there is no closed Thumbnail to bring back"));
        // On failure the stack has already raised the notification.
        return stack.restore() ? Reply::success() : Reply::failure(QStringLiteral("the Thumbnail could not be restored"));
    });
});

} // namespace tarn
