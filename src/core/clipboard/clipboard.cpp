#include "clipboard.h"

#include "daemon/daemon.h"
#include "process/process_runner.h"
#include "storage/storage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace tarn {

static constexpr qint64 kMaxAgeSeconds = 24 * 60 * 60;
static constexpr int kSweepEveryMs = 60 * 60 * 1000;

Clipboard::Clipboard(Daemon &daemon) : m_daemon(daemon) {
    sweep(); // start-up
    m_timer.setInterval(kSweepEveryMs);
    connect(&m_timer, &QTimer::timeout, this, [this] { sweep(); });
    m_timer.start();
}

QString Clipboard::sharedDir() {
    QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (cache.isEmpty())
        cache = QDir::homePath() + QStringLiteral("/.cache");
    return cache + QStringLiteral("/tarn/shared");
}

QString Clipboard::copyPng(const QByteArray &png, const QDateTime &captureTime, QString *error, const CaptureName &name) {
    const QString dir = sharedDir();
    if (!QDir().mkpath(dir)) {
        *error = QStringLiteral("could not create the folder %1").arg(dir);
        return {};
    }
    const QString path = Storage::writeNew(dir, m_daemon.service<Storage>().baseName(captureTime, name), QStringLiteral("png"), png, error);
    if (path.isEmpty())
        return {};

    if (!runHelper({path}, error)) {
        QFile::remove(path);
        return {};
    }
    return path;
}

bool Clipboard::copyFile(const QString &absolutePath, QString *error) {
    return runHelper({QStringLiteral("--file-only"), absolutePath}, error);
}

bool Clipboard::copyText(const QString &text, QString *error) {
    const QString dir = sharedDir();
    if (!QDir().mkpath(dir)) {
        *error = QStringLiteral("could not create the folder %1").arg(dir);
        return false;
    }
    // The helper reads the file once when it starts; it is not needed after.
    QTemporaryFile file(dir + QStringLiteral("/text-XXXXXX.txt"));
    if (!file.open() || file.write(text.toUtf8()) < 0 || !file.flush()) {
        *error = QStringLiteral("could not write a temporary file in %1: %2").arg(dir, file.errorString());
        return false;
    }
    return runHelper({QStringLiteral("--text"), file.fileName()}, error);
}

// tarn-clip sets the clipboard, then detaches and keeps serving it after this
// call returns (and after the daemon quits). PATH first so tests can stub it;
// otherwise the one built next to `tarn`.
bool Clipboard::runHelper(const QStringList &args, QString *error) {
    QString program = QStandardPaths::findExecutable(QStringLiteral("tarn-clip"));
    if (program.isEmpty())
        program = QStandardPaths::findExecutable(QStringLiteral("tarn-clip"), {QCoreApplication::applicationDirPath()});
    if (program.isEmpty())
        program = QStringLiteral("tarn-clip");
    const ProcessResult result = m_daemon.service<ProcessRunner>().run(program, args);
    if (!result.succeeded()) {
        *error = result.describe(QStringLiteral("tarn-clip"));
        return false;
    }
    return true;
}

int Clipboard::sweep() {
    const QDateTime cutoff = m_daemon.now().addSecs(-kMaxAgeSeconds);
    int removed = 0;
    const auto files = QDir(sharedDir()).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : files)
        if (info.lastModified() < cutoff && QFile::remove(info.absoluteFilePath()))
            ++removed;
    return removed;
}

// Sweep once when the daemon starts; the service then repeats it hourly.
TARN_MODULE(clipboard, [](Daemon &daemon) { daemon.service<Clipboard>(); });

} // namespace tarn
