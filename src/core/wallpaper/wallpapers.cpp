#include "wallpapers.h"

#include "daemon/daemon.h"
#include "settings/settings.h"
#include "settings/watched_file.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QUrl>
#include <algorithm>

namespace tarn {

namespace {

// The first line of a small text file holding a path ("" if absent or blank).
QString firstLine(const QString &file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QString line = QString::fromUtf8(f.read(4096)).section(QLatin1Char('\n'), 0, 0).trimmed();
    if (line.startsWith(QLatin1String("file://")))
        line = QUrl(line).toLocalFile();
    if (line == QLatin1String("~") || line.startsWith(QLatin1String("~/")))
        line = QDir::homePath() + line.mid(1);
    return line;
}

} // namespace

Wallpapers::Wallpapers(Daemon &daemon) : QObject(nullptr), m_settings(daemon.service<Settings>()) {
    connect(&m_settings, &Settings::changed, this, [this](const QString &key) {
        if (key == QLatin1String("wallpaper_folder")) {
            emit changed();
        } else if (key == QLatin1String("wallpaper_file")) {
            m_file.setPath(m_settings.wallpaperFile());
            emit changed();
        }
    });
    connect(&m_file, &WatchedFile::changed, this, [this] {
        m_file.rewatch();
        emit changed();
    });
    m_file.setPath(m_settings.wallpaperFile());
}

void Wallpapers::setCurrentSource(CurrentSource source) {
    m_source = std::move(source);
    emit changed();
}

QString Wallpapers::fileCurrentWallpaper() const {
    const QString file = m_settings.wallpaperFile();
    return file.isEmpty() ? QString() : firstLine(file);
}

bool Wallpapers::isImageFile(const QString &path) {
    const QFileInfo info(path);
    if (!info.isFile())
        return false;
    static const QStringList suffixes = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("webp"),
                                         QStringLiteral("bmp"), QStringLiteral("gif")};
    return suffixes.contains(info.suffix().toLower()) && QImageReader(path).canRead();
}

QString Wallpapers::current() const {
    const QString path = m_source ? m_source() : fileCurrentWallpaper();
    return !path.isEmpty() && isImageFile(path) ? QFileInfo(path).absoluteFilePath() : QString();
}

QStringList Wallpapers::folderImages() const {
    const QDir dir(m_settings.wallpaperFolder());
    QStringList files;
    for (const QFileInfo &info : dir.entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort))
        if (isImageFile(info.absoluteFilePath()))
            files << info.absoluteFilePath();
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(files.begin(), files.end(), [&](const QString &a, const QString &b) {
        return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
    });
    return files;
}

QVariantList Wallpapers::list() const {
    QVariantList out;
    const QString now = current();
    auto add = [&](const QString &path, bool isCurrent) {
        out << QVariantMap{{QStringLiteral("path"), path},
                           {QStringLiteral("label"), QFileInfo(path).completeBaseName()},
                           {QStringLiteral("current"), isCurrent}};
    };
    if (!now.isEmpty())
        add(now, true);
    const QString nowCanonical = now.isEmpty() ? QString() : QFileInfo(now).canonicalFilePath();
    for (const QString &path : folderImages())
        if (nowCanonical.isEmpty() || QFileInfo(path).canonicalFilePath() != nowCanonical)
            add(path, false);
    return out;
}

} // namespace tarn
