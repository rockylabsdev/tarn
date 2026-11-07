#pragma once
// Copy: puts a capture on the clipboard through tarn-clip (a detached helper,
// because wl-copy offers only one MIME type per call), backed by a PNG file
// in ~/.cache/tarn/shared/ so the clipboard outlives the Thumbnail and Tarn.
// The same folder is swept at start-up and every hour: files older than a day go.
#include "storage/file_name.h"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QTimer>

namespace tarn {

class Daemon;

class Clipboard : public QObject {
    Q_OBJECT
public:
    explicit Clipboard(Daemon &daemon);

    // $XDG_CACHE_HOME/tarn/shared
    static QString sharedDir();
    // Writes the PNG to the shared folder (named by the File name pattern, _2/_3 on a
    // clash, never overwriting) and puts it on the clipboard as image/png plus
    // the file itself. Returns the file path, or empty with *error set. Raises
    // no notification: callers decide.
    QString copyPng(const QByteArray &png, const QDateTime &captureTime, QString *error, const CaptureName &name = {});

    // Puts an existing file on the clipboard as a file only (no image/png),
    // e.g. a Recording. The file must stay put for as long as it may be pasted.
    bool copyFile(const QString &absolutePath, QString *error);

    // Puts text on the clipboard as plain text (Text capture). The helper keeps
    // its own copy, so the clipboard outlives Tarn. Raises no notification.
    bool copyText(const QString &text, QString *error);

    // Deletes files in the shared folder last modified more than a day before
    // daemon.now(). Returns how many it removed.
    int sweep();

private:
    bool runHelper(const QStringList &args, QString *error);
    Daemon &m_daemon;
    QTimer m_timer;
};

} // namespace tarn
