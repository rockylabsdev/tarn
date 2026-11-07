#pragma once
// Where a Recording's files live. A running or fresh Recording is written to the
// kept folder (~/.cache/tarn/recordings, on disk: /tmp is RAM here). It stays
// there while the Recording can still be restored, and is deleted once it can
// not. Save copies it to the Recordings folder (<Videos>/Tarn); Copy gives the
// clipboard a hard link in the shared folder, so a paste outlives the kept file.
// After a crash, whatever is still in the kept folder and was never saved is
// saved to the Recordings folder at the next start.
#include "storage/file_name.h"

#include <QDateTime>
#include <QObject>
#include <QString>

namespace tarn {

class Daemon;

class RecordingStore : public QObject {
    Q_OBJECT
public:
    explicit RecordingStore(Daemon &daemon) : m_daemon(daemon) {}

    // $XDG_CACHE_HOME/tarn/recordings
    static QString keptDir();
    // The Recordings folder Setting (default <Videos>/Tarn).
    QString recordingsDir() const;

    // A fresh path in the kept folder named by the File name pattern (2026-11-03_09-30-15.mp4 by default,
    // then _2, _3 ...), the folder created. Empty with *error set on failure.
    QString newKeptPath(const QDateTime &captureTime, const QString &extension, QString *error, const CaptureName &name = {});

    // Copies a kept file to the Recordings folder under its own name (_2, _3 on a
    // clash, never overwriting) and marks it saved. Returns the path written.
    QString saveToFolder(const QString &keptPath, QString *error);
    bool isSaved(const QString &keptPath) const;

    // Gives the clipboard something that stays: a hard link (else a copy) of the
    // kept file in the shared folder. Returns its path.
    QString shareCopy(const QString &keptPath, QString *error);

    // The Recording can no longer be restored: deletes the kept file (and its
    // saved marker). Only ever deletes inside the kept folder.
    void discard(const QString &keptPath);

    // Start-up after a crash: saves every kept file that was never saved to the
    // Recordings folder, empties the kept folder, and raises ONE notification if
    // any were saved. Returns how many.
    int recover();

private:
    bool inKeptDir(const QString &path) const;
    Daemon &m_daemon;
};

} // namespace tarn
