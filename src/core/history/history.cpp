#include "history.h"

#include "daemon/daemon.h"
#include "document/capture_document.h"
#include "document/project_file.h"
#include "notify/notifier.h"
#include "settings/settings.h"
#include "storage/image_file.h"
#include "storage/storage.h"
#include "state/remembered_state.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

namespace tarn {

static QString dataHome() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
}

static QString trashDir() { return dataHome() + QStringLiteral("/Trash"); }

static bool writeAtomically(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

static QJsonObject readJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

static QDateTime captureTimeOf(const QJsonObject &description) {
    return QDateTime::fromString(description.value(QStringLiteral("captureTime")).toString(), Qt::ISODate);
}

History::History(Daemon &daemon) : m_daemon(daemon) {
    m_pool.setMaxThreadCount(1);
    connect(&daemon.service<Settings>(), &Settings::changed, this, [this](const QString &key) {
        if (key == QLatin1String("keep_history_for"))
            sweep(); // the first sweep after a change of retention
    });
    sweep(); // start-up
    m_sweepTimer.setInterval(6 * 60 * 60 * 1000);
    connect(&m_sweepTimer, &QTimer::timeout, this, [this] { sweep(); });
    m_sweepTimer.start();
}

History::~History() { m_pool.waitForDone(); }

QString History::dir() { return dataHome() + QStringLiteral("/tarn/history"); }

void History::flush() {
    m_pool.waitForDone();
    // The change events queued by the writes that just finished.
    QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
}

QString History::record(const QByteArray &png, const CaptureInfo &info) {
    QDir().mkpath(dir());
    const QString base = info.time.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    QString id;
    {
        QMutexLocker lock(&m_mutex);
        for (int n = 1;; ++n) {
            id = n == 1 ? base : QStringLiteral("%1_%2").arg(base).arg(n);
            if (!m_reserved.contains(id) && !QFile::exists(pngPath(id)) && !QFile::exists(descriptionPath(id)))
                break;
        }
        m_reserved.insert(id);
    }

    QJsonObject description{
        {QStringLiteral("captureTime"), info.time.toString(Qt::ISODate)},
        {QStringLiteral("kind"), info.kind},
        {QStringLiteral("position"),
         QJsonObject{{QStringLiteral("x"), info.position.x()}, {QStringLiteral("y"), info.position.y()},
                     {QStringLiteral("width"), info.position.width()}, {QStringLiteral("height"), info.position.height()}}},
        {QStringLiteral("savedPaths"), QJsonArray()},
    };
    if (!info.app.isEmpty())
        description.insert(QStringLiteral("app"), info.app);
    if (!info.title.isEmpty())
        description.insert(QStringLiteral("title"), info.title);

    const QString pngFile = pngPath(id), jsonFile = descriptionPath(id);
    const QByteArray jsonBytes = QJsonDocument(description).toJson();
    m_pool.start([this, id, description, png, pngFile, jsonFile, jsonBytes] {
        // The PNG goes first: a description never exists without its picture.
        if (writeAtomically(pngFile, png) && writeAtomically(jsonFile, jsonBytes))
            QMetaObject::invokeMethod(this, [this, id, description] { emit entryAdded(id, description); }, Qt::QueuedConnection);
    });
    return id;
}

void History::setText(const QString &id, const QString &text) {
    const QString jsonFile = descriptionPath(id);
    m_pool.start([this, id, jsonFile, text] {
        QJsonObject description = readJson(jsonFile);
        if (description.isEmpty())
            return;
        description.insert(QStringLiteral("text"), text);
        if (writeAtomically(jsonFile, QJsonDocument(description).toJson()))
            QMetaObject::invokeMethod(this, [this, id, description] {
                emit textRead(id);
                emit entryChanged(id, description);
            }, Qt::QueuedConnection);
    });
}

void History::addSavedPath(const QString &id, const QString &path) {
    const QString jsonFile = descriptionPath(id);
    m_pool.start([this, id, jsonFile, path] {
        QJsonObject description = readJson(jsonFile);
        if (description.isEmpty())
            return;
        QJsonArray saved = description.value(QStringLiteral("savedPaths")).toArray();
        saved.append(path);
        description.insert(QStringLiteral("savedPaths"), saved);
        if (writeAtomically(jsonFile, QJsonDocument(description).toJson()))
            QMetaObject::invokeMethod(this, [this, id, description] { emit entryChanged(id, description); }, Qt::QueuedConnection);
    });
}

QStringList History::ids() {
    flush();
    QList<std::pair<QDateTime, QString>> found;
    for (const QString &name : QDir(dir()).entryList({QStringLiteral("*.json")}, QDir::Files)) {
        if (name.endsWith(QLatin1String(".edit.json"))) // a Project file, not an entry of its own
            continue;
        const QString id = name.chopped(5);
        found.append({captureTimeOf(readJson(descriptionPath(id))), id});
    }
    std::sort(found.begin(), found.end());
    QStringList out;
    for (const auto &f : found)
        out << f.second;
    return out;
}

bool History::isBusy(const QString &id) const {
    for (const auto &check : m_busyChecks)
        if (check(id))
            return true;
    return false;
}

QDateTime History::retentionCutoff(const QDateTime &now) const {
    return m_daemon.service<Settings>().retentionCutoff(now).value_or(QDateTime());
}

// What the previous sweep applied is remembered ("history.retention"), so a
// shortened retention can tell newly expired entries from long-expired ones.
// Nothing remembered means the default, 1 month.
int History::sweep() {
    flush();
    auto &settings = m_daemon.service<Settings>();
    auto &state = m_daemon.service<RememberedState>();
    const QDateTime now = m_daemon.now();
    const QString current = settings.keepHistoryFor();
    const QString applied = state.value(QStringLiteral("history.retention"), QStringLiteral("1 month")).toString();
    const auto cutoff = Settings::cutoffFor(current, now);
    const auto appliedCutoff = Settings::cutoffFor(applied, now);

    int removed = 0;
    bool skipped = false;
    if (cutoff) {
        for (const QString &id : ids()) {
            const QDateTime captured = captureTimeOf(readJson(descriptionPath(id)));
            if (!captured.isValid() || captured >= *cutoff)
                continue;
            if (isBusy(id)) { // open in an Editor or on the stack: the first sweep after it closes takes it
                skipped = true;
                continue;
            }
            const bool newlyExpired = !appliedCutoff || captured >= *appliedCutoff;
            if (newlyExpired) {
                if (!trash(id))
                    continue; // stays in History; the next sweep tries again
            } else {
                QFile::remove(pngPath(id));
                QFile::remove(descriptionPath(id));
                QFile::remove(projectPath(id));
            }
            {
                QMutexLocker lock(&m_mutex);
                m_reserved.remove(id);
            }
            ++removed;
            emit entryRemoved(id);
        }
    }
    // A skipped entry is judged against the same applied retention when it is finally taken.
    if (applied != current && !skipped)
        state.setValue(QStringLiteral("history.retention"), current);
    return removed;
}

// ---- Trash (freedesktop.org Trash specification, home trash) ----

static QString infoFile(const QString &name) {
    return trashDir() + QStringLiteral("/info/") + name + QStringLiteral(".trashinfo");
}

static QString uniqueTrashName(const QString &name) {
    const QFileInfo fi(name);
    for (int n = 1;; ++n) {
        const QString candidate =
            n == 1 ? name : QStringLiteral("%1_%2.%3").arg(fi.completeBaseName()).arg(n).arg(fi.suffix());
        if (!QFile::exists(trashDir() + QStringLiteral("/files/") + candidate) && !QFile::exists(infoFile(candidate)))
            return candidate;
    }
}

static bool moveFile(const QString &from, const QString &to) {
    if (QFile::rename(from, to))
        return true;
    // Another filesystem: copy, then remove.
    return QFile::copy(from, to) && QFile::remove(from);
}

static bool trashOne(const QString &path, const QDateTime &now) {
    if (!QDir().mkpath(trashDir() + QStringLiteral("/files")) || !QDir().mkpath(trashDir() + QStringLiteral("/info")))
        return false;
    const QString name = uniqueTrashName(QFileInfo(path).fileName());
    const QByteArray info = "[Trash Info]\nPath=" + QUrl::toPercentEncoding(path, "/") + "\nDeletionDate=" +
                            now.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")).toUtf8() + "\n";
    // The info file first, as the specification asks.
    if (!writeAtomically(infoFile(name), info))
        return false;
    if (!moveFile(path, trashDir() + QStringLiteral("/files/") + name)) {
        QFile::remove(infoFile(name));
        return false;
    }
    return true;
}

static bool restoreOne(const QString &originalPath) {
    const QDir infoDir(trashDir() + QStringLiteral("/info"));
    for (const QString &infoName : infoDir.entryList({QStringLiteral("*.trashinfo")}, QDir::Files)) {
        QFile f(infoDir.filePath(infoName));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QString path;
        for (const QByteArray &line : f.readAll().split('\n'))
            if (line.startsWith("Path="))
                path = QUrl::fromPercentEncoding(line.mid(5).trimmed());
        f.close();
        if (path != originalPath)
            continue;
        const QString trashed = trashDir() + QStringLiteral("/files/") + infoName.chopped(10);
        if (!QFile::exists(trashed) || QFile::exists(originalPath) || !QDir().mkpath(QFileInfo(originalPath).path()))
            return false;
        if (!moveFile(trashed, originalPath))
            return false;
        QFile::remove(infoDir.filePath(infoName));
        return true;
    }
    return false;
}

bool History::trash(const QString &id) {
    flush();
    if (!QFile::exists(descriptionPath(id)))
        return false;
    const QDateTime now = m_daemon.now();
    // Both files or neither: undo the first if the second fails.
    if (!trashOne(pngPath(id), now))
        return false;
    if (!trashOne(descriptionPath(id), now)) {
        restoreOne(pngPath(id));
        return false;
    }
    // The Project file, when there is one, goes with its group.
    if (QFile::exists(projectPath(id)) && !trashOne(projectPath(id), now)) {
        restoreOne(descriptionPath(id));
        restoreOne(pngPath(id));
        return false;
    }
    emit entryRemoved(id);
    return true;
}

bool History::restore(const QString &id) {
    flush();
    if (!restoreOne(pngPath(id)))
        return false;
    if (!restoreOne(descriptionPath(id)))
        return false;
    restoreOne(projectPath(id)); // only there if the entry had one
    emit entryAdded(id, readJson(descriptionPath(id)));
    return true;
}

// ---- The Project file ----

bool History::isEdited(const QString &id) const { return QFile::exists(projectPath(id)); }

bool History::hasProject(const QString &id) {
    flush();
    return isEdited(id);
}

QString History::projectText(const QString &id) const {
    QFile file(projectPath(id));
    return file.open(QIODevice::ReadOnly) ? tarn::projectText(file.readAll()) : QString();
}

static bool projectReadable(const QByteArray &bytes) {
    CaptureDocument scratch{QImage(1, 1, QImage::Format_ARGB32)};
    return loadProject(scratch, bytes).ok;
}

static QByteArray readAll(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void History::saveProject(const QString &id, const CaptureDocument &document) {
    const QString path = projectPath(id);
    const bool edited = document.isEdited();
    const QByteArray bytes = edited ? projectToJson(document) : QByteArray();
    const QDateTime now = m_daemon.now();
    const QString jsonFile = descriptionPath(id);
    m_pool.start([this, id, path, jsonFile, edited, bytes, now] {
        if (QFile::exists(path) && !projectReadable(readAll(path)))
            trashOne(path, now); // a damaged one is kept in the Trash, never overwritten
        if (edited)
            writeAtomically(path, bytes);
        else
            QFile::remove(path);
        // The grid and search follow the Project file (the Edited mark, the Text annotations).
        const QJsonObject description = readJson(jsonFile);
        if (!description.isEmpty())
            QMetaObject::invokeMethod(this, [this, id, description] { emit entryChanged(id, description); }, Qt::QueuedConnection);
    });
}

History::LoadedProject History::loadProject(const QString &id, const QImage &original) {
    flush();
    LoadedProject out;
    const QString path = projectPath(id);
    if (!QFile::exists(path))
        return out;
    auto document = std::make_unique<CaptureDocument>(original);
    const ProjectLoad load = tarn::loadProject(*document, readAll(path));
    auto &notifier = m_daemon.service<Notifier>();
    if (!load.ok) {
        out.state = ProjectState::Unreadable;
        notifier.notify(QStringLiteral("Tarn could not read the edits of a capture"),
                        QStringLiteral("%1 is damaged, so the original is shown. The next Done replaces it.").arg(QFileInfo(path).fileName()));
        return out;
    }
    out.state = ProjectState::Loaded;
    if (load.backgroundMissing)
        notifier.notify(QStringLiteral("Frame background not found"),
                        QStringLiteral("%1 is gone, so the Frame uses a blurred copy of the shot.").arg(load.missingFile));
    out.document = std::move(document);
    return out;
}

QJsonObject History::description(const QString &id) {
    flush();
    return readJson(descriptionPath(id));
}

CaptureName History::captureName(const QString &id) {
    const QJsonObject d = description(id);
    const QString kind = normalizedKind(d.value(QStringLiteral("kind")).toString());
    return {kind == QLatin1String("fullscreen") ? QStringLiteral("screen") : kind, d.value(QStringLiteral("app")).toString(),
            d.value(QStringLiteral("title")).toString()};
}

QString History::saveImage(const QString &id, const QByteArray &png, const QDateTime &captureTime, const CaptureName &name) {
    QString error;
    const QString path = m_daemon.service<Storage>().saveScreenshot(png, captureTime, &error, name);
    if (path.isEmpty()) {
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not save the screenshot"), error);
        return {};
    }
    if (!id.isEmpty())
        addSavedPath(id, path);
    return path;
}

bool History::saveImageAs(const QString &id, const QByteArray &png, const QString &path) {
    QString error;
    if (!writeImageFile(png, path, &error)) {
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not save the screenshot"), error);
        return false;
    }
    m_daemon.service<RememberedState>().setValue(QStringLiteral("save_as.folder"), QFileInfo(path).absolutePath());
    if (!id.isEmpty())
        addSavedPath(id, path);
    return true;
}

QString History::saveAsFileName(const QString &id) {
    auto &storage = m_daemon.service<Storage>();
    const QDateTime time = QDateTime::fromString(description(id).value(QStringLiteral("captureTime")).toString(), Qt::ISODate);
    return storage.baseName(time, captureName(id)) + '.' + storage.imageExtension();
}

QByteArray History::originalPng(const QString &id) {
    flush();
    return readAll(pngPath(id));
}

QByteArray History::editedPng(const QString &id) {
    flush();
    if (!QFile::exists(projectPath(id)))
        return {};
    const QImage original = QImage::fromData(readAll(pngPath(id)), "PNG");
    if (original.isNull())
        return {};
    const LoadedProject loaded = loadProject(id, original);
    return loaded.document ? loaded.document->toPng() : QByteArray();
}

bool History::revertProject(const QString &id) {
    flush();
    if (!QFile::exists(projectPath(id)) || !trashOne(projectPath(id), m_daemon.now()))
        return false;
    const QJsonObject d = readJson(descriptionPath(id));
    if (!d.isEmpty())
        emit entryChanged(id, d);
    return true;
}

// History starts with the daemon, so its start-up sweep and 6-hour timer run.
TARN_MODULE(history, [](Daemon &daemon) { daemon.service<History>(); });

} // namespace tarn
