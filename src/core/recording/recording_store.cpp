#include "recording_store.h"

#include "clipboard/clipboard.h"
#include "daemon/daemon.h"
#include "notify/notifier.h"
#include "settings/settings.h"
#include "storage/storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <sys/stat.h>
#include <unistd.h>

namespace tarn {

namespace {
const QString kSavedMarker = QStringLiteral(".saved");

// Writes `source` into `dir` as <base>.<ext>, <base>_2.<ext> ... : never overwrites.
QString copyUnique(const QString &source, const QString &dir, QString *error) {
    if (!QDir().mkpath(dir)) {
        *error = QStringLiteral("could not create the folder %1").arg(dir);
        return {};
    }
    const QFileInfo info(source);
    for (int n = 1; n < 10000; ++n) {
        const QString name = n == 1 ? info.completeBaseName() : QStringLiteral("%1_%2").arg(info.completeBaseName()).arg(n);
        const QString path = dir + '/' + name + '.' + info.suffix();
        if (QFileInfo::exists(path))
            continue;
        // Copied under a temporary name first, so a half-written file never has the final one.
        const QString partial = path + QStringLiteral(".part");
        QFile::remove(partial);
        if (!QFile::copy(source, partial) || !QFile::rename(partial, path)) {
            *error = QStringLiteral("could not write %1").arg(path);
            QFile::remove(partial);
            return {};
        }
        return path;
    }
    *error = QStringLiteral("too many files named like %1 in %2").arg(info.fileName(), dir);
    return {};
}

// The same file on disk (a hard link), not merely the same name.
bool sameFile(const QString &a, const QString &b) {
    struct stat sa, sb;
    return ::stat(QFile::encodeName(a).constData(), &sa) == 0 && ::stat(QFile::encodeName(b).constData(), &sb) == 0 &&
           sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}
} // namespace

QString RecordingStore::keptDir() {
    QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (cache.isEmpty())
        cache = QDir::homePath() + QStringLiteral("/.cache");
    return cache + QStringLiteral("/tarn/recordings");
}

QString RecordingStore::recordingsDir() const {
    return m_daemon.service<Settings>().recordingsFolder();
}

bool RecordingStore::inKeptDir(const QString &path) const {
    return QFileInfo(path).absolutePath() == QFileInfo(keptDir()).absoluteFilePath();
}

QString RecordingStore::newKeptPath(const QDateTime &captureTime, const QString &extension, QString *error, const CaptureName &name) {
    const QString dir = keptDir();
    if (!QDir().mkpath(dir)) {
        *error = QStringLiteral("could not create the folder %1").arg(dir);
        return {};
    }
    const QString base = m_daemon.service<Storage>().baseName(captureTime, name);
    for (int n = 1; n < 10000; ++n) {
        const QString unique = n == 1 ? base : QStringLiteral("%1_%2").arg(base).arg(n);
        const QString path = dir + '/' + unique + '.' + extension;
        if (!QFileInfo::exists(path))
            return path;
    }
    *error = QStringLiteral("too many recordings named %1").arg(base);
    return {};
}

QString RecordingStore::saveToFolder(const QString &keptPath, QString *error) {
    const QString path = copyUnique(keptPath, recordingsDir(), error);
    if (path.isEmpty())
        return {};
    // A marker beside the kept file: after a crash it must not be saved a second time.
    QFile marker(keptPath + kSavedMarker);
    static_cast<void>(marker.open(QIODevice::WriteOnly)); // if this fails the file is still saved; a crash could then save it twice
    return path;
}

bool RecordingStore::isSaved(const QString &keptPath) const {
    return QFileInfo::exists(keptPath + kSavedMarker);
}

QString RecordingStore::shareCopy(const QString &keptPath, QString *error) {
    const QString dir = Clipboard::sharedDir();
    if (!QDir().mkpath(dir)) {
        *error = QStringLiteral("could not create the folder %1").arg(dir);
        return {};
    }
    // A name already in the folder is the same Recording only if it is the same
    // file. After a Trim the kept file is a new file, and what an earlier Copy
    // gave the clipboard must stay as it was: the new one gets the next name.
    const QFileInfo kept(keptPath);
    for (int n = 1; n < 10000; ++n) {
        const QString name = n == 1 ? kept.completeBaseName() : QStringLiteral("%1_%2").arg(kept.completeBaseName()).arg(n);
        const QString path = dir + '/' + name + '.' + kept.suffix();
        if (QFileInfo::exists(path)) {
            if (sameFile(path, keptPath))
                return path; // copied before: the same Recording
            continue;
        }
        if (::link(QFile::encodeName(keptPath).constData(), QFile::encodeName(path).constData()) != 0 && !QFile::copy(keptPath, path)) {
            *error = QStringLiteral("could not copy %1 for the clipboard").arg(kept.fileName());
            return {};
        }
        return path;
    }
    *error = QStringLiteral("too many copies of %1 for the clipboard").arg(kept.fileName());
    return {};
}

void RecordingStore::discard(const QString &keptPath) {
    if (keptPath.isEmpty() || !inKeptDir(keptPath))
        return;
    QFile::remove(keptPath);
    QFile::remove(keptPath + kSavedMarker);
}

int RecordingStore::recover() {
    const QDir dir(keptDir());
    int saved = 0;
    QString lastError;
    for (const QFileInfo &file : dir.entryInfoList(QDir::Files, QDir::Name)) {
        const QString path = file.absoluteFilePath();
        if (!QFileInfo::exists(path))
            continue; // taken care of with its partner
        if (path.endsWith(kSavedMarker))
            continue; // goes with its file
        if (path.endsWith(QStringLiteral(".part"))) { // a GIF that was still being made
            QFile::remove(path);
            continue;
        }
        if (path.endsWith(QStringLiteral(".mp4")) && QFileInfo::exists(path.chopped(4) + QStringLiteral(".gif"))) {
            discard(path); // a GIF Recording's source: the GIF is what gets saved
            continue;
        }
        if (isSaved(path)) {
            discard(path); // saved already; only the restore slot was keeping it
            continue;
        }
        if (file.size() == 0) { // a recorder that never wrote anything
            discard(path);
            continue;
        }
        QString error;
        if (saveToFolder(path, &error).isEmpty()) {
            lastError = error; // stays where it is; the next start tries again
            continue;
        }
        ++saved;
        discard(path);
        if (path.endsWith(QStringLiteral(".gif")))
            discard(path.chopped(4) + QStringLiteral(".mp4")); // the GIF Recording's source is not saved as well
    }
    // Markers whose file is gone.
    for (const QFileInfo &stray : dir.entryInfoList({QStringLiteral("*") + kSavedMarker}, QDir::Files))
        if (!QFileInfo::exists(stray.absoluteFilePath().chopped(kSavedMarker.size())))
            QFile::remove(stray.absoluteFilePath());
    if (saved > 0)
        m_daemon.service<Notifier>().notify(
            saved == 1 ? QStringLiteral("Tarn saved a recording from before it stopped")
                       : QStringLiteral("Tarn saved %1 recordings from before it stopped").arg(saved),
            QStringLiteral("In %1").arg(recordingsDir()));
    else if (!lastError.isEmpty())
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not save a recording from before it stopped"), lastError);
    return saved;
}

} // namespace tarn
