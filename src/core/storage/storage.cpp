#include "storage.h"

#include "daemon/daemon.h"
#include "image_file.h"
#include "settings/settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace tarn {

QString Storage::screenshotDir() const { return m_daemon.service<Settings>().screenshotFolder(); }

QString Storage::timestampName(const QDateTime &t) {
    return t.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
}

QString Storage::baseName(const QDateTime &captureTime, const CaptureName &name) const {
    return fileBaseName(m_daemon.service<Settings>().fileNamePattern(), captureTime, name);
}

QString Storage::imageExtension() const {
    const QString format = m_daemon.service<Settings>().imageFormat();
    return format == QLatin1String("jpeg") ? QStringLiteral("jpg") : format;
}

int Storage::imageFormatIndex() const {
    const QString format = m_daemon.service<Settings>().imageFormat();
    return format == QLatin1String("jpeg") ? 1 : format == QLatin1String("webp") ? 2 : 0;
}

QString Storage::saveScreenshot(const QByteArray &png, const QDateTime &captureTime, QString *error, const CaptureName &name) {
    const QString extension = imageExtension();
    const QByteArray bytes = encodeImage(png, formatForSuffix(extension), error);
    if (bytes.isEmpty())
        return {};
    return writeNew(screenshotDir(), baseName(captureTime, name), extension, bytes, error);
}

QString Storage::writeNew(const QString &dir, const QString &base, const QString &extension, const QByteArray &bytes,
                          QString *error) {
    if (!QDir().mkpath(dir)) {
        *error = QStringLiteral("could not create the folder %1").arg(dir);
        return {};
    }
    for (int n = 1; n < 10000; ++n) {
        const QString name = n == 1 ? base : QStringLiteral("%1_%2").arg(base).arg(n);
        const QString path = dir + '/' + name + '.' + extension;
        QFile file(path);
        // NewOnly fails if the file exists, so a clash can never overwrite.
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (QFileInfo::exists(path))
                continue;
            *error = QStringLiteral("could not write %1: %2").arg(path, file.errorString());
            return {};
        }
        if (file.write(bytes) != bytes.size() || !file.flush()) {
            *error = QStringLiteral("could not write %1: %2").arg(path, file.errorString());
            file.close();
            QFile::remove(path);
            return {};
        }
        return path;
    }
    *error = QStringLiteral("too many files named %1 in %2").arg(base, dir);
    return {};
}

} // namespace tarn
