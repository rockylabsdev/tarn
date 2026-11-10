#include "thumbnail_stack.h"

#include "clipboard/clipboard.h"
#include "daemon/daemon.h"
#include "document/capture_document.h"
#include "history/history.h"
#include "hyprland/hyprland_bridge.h"
#include "notify/notifier.h"
#include "preset/preset_store.h"
#include "recording/recording_store.h"
#include "settings/settings.h"
#include "state/remembered_state.h"
#include "storage/image_file.h"
#include "storage/storage.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <utility>

namespace tarn {

ThumbnailStack::ThumbnailStack(Daemon &daemon) : m_daemon(daemon) {
    // The History sweep leaves an entry alone while it is on the stack (hidden ones too).
    daemon.service<History>().addBusyCheck([this](const QString &id) { return holdsHistoryEntry(id); });
}

int ThumbnailStack::count() const {
    int n = 0;
    for (const auto &e : m_entries)
        n += e.hidden ? 0 : 1;
    return n;
}

int ThumbnailStack::add(const QByteArray &png, const QDateTime &captureTime, const CaptureName &name, bool framed) {
    return add(QImage::fromData(png, "PNG"), QtFuture::makeReadyValueFuture(png), captureTime, name, framed);
}

int ThumbnailStack::add(const QImage &shot, const QFuture<QByteArray> &png, const QDateTime &captureTime, const CaptureName &name,
                        bool framed) {
    if (shot.isNull())
        return -1;
    QImage image = shot;
    Entry entry;
    entry.pendingPng = png;
    entry.original = shot;
    // The auto-applied Preset: the Frame is part of the capture from the start,
    // so the card, auto Copy / Save and the overflow auto-save all use the framed image
    // (History keeps the untouched original). B in the Overlay passes framed = false.
    if (framed) {
        if (const auto frame = m_daemon.service<PresetStore>().autoFrame()) {
            CaptureDocument document(image);
            document.setFrameLive(*frame);
            const QByteArray framedPng = document.toPng();
            const QImage framedImage = QImage::fromData(framedPng, "PNG");
            if (!framedImage.isNull()) {
                entry.edited = framedPng;
                entry.autoFrame = *frame;
                image = framedImage;
            }
        }
    }
    entry.image = std::move(image);
    entry.captureTime = captureTime;
    entry.name = name;
    const int id = insert(std::move(entry));
    Entry *added = entryFor(id);
    if (!added)
        return id;
    added->settingsPending = true;
    if (png.isFinished())
        settle(*added);
    else
        QFuture<QByteArray>(png).then(this, [this, id](const QByteArray &) {
            if (Entry *e = entryFor(id))
                settle(*e);
        });
    armAutoClose(id);
    return id;
}

int ThumbnailStack::addRecording(const QByteArray &posterPng, const QDateTime &captureTime, const QString &videoPath, qint64 durationMs,
                                const QString &sourcePath, const CaptureName &name) {
    QImage image = QImage::fromData(posterPng, "PNG");
    if (image.isNull())
        return -1;
    Entry entry;
    entry.png = posterPng;
    entry.image = std::move(image);
    entry.captureTime = captureTime;
    entry.name = name;
    entry.kind = Recording;
    entry.videoPath = videoPath;
    entry.sourcePath = sourcePath;
    entry.durationMs = durationMs;
    return insert(std::move(entry));
}

int ThumbnailStack::insert(Entry entry) {
    entry.id = m_nextId++;
    entry.hidden = false;
    const int id = entry.id;
    const int row = count();
    beginInsertRows({}, row, row);
    m_entries.push_back(std::move(entry));
    endInsertRows();
    emit countChanged();
    emit added(id);
    enforceLimit();
    return id;
}

ThumbnailStack::Entry *ThumbnailStack::settledEntry(int id) {
    Entry *entry = entryFor(id);
    if (entry)
        settle(*entry);
    return entry;
}

// The PNG is ready (or is waited for now): the Thumbnail works from it from now on,
// and Copy / Save automatically run, which were held back until it existed.
void ThumbnailStack::settle(Entry &entry) {
    if (entry.pendingPng.isValid()) {
        const QByteArray png = entry.pendingPng.result();
        entry.pendingPng = {};
        if (!png.isEmpty()) {
            entry.png = png;
            entry.original = {};
        } else { // encoding failed: made here instead, so nothing ever works from a missing file
            QBuffer buffer(&entry.png);
            buffer.open(QIODevice::WriteOnly);
            entry.original.save(&buffer, "PNG");
            entry.original = {};
        }
    }
    if (std::exchange(entry.settingsPending, false))
        applyCopySave(entry);
}

// The Settings that act on a fresh capture: Copy / Save automatically (the
// Thumbnail stays on screen either way). The auto-close timer is armed in add().
// A Recording gets none of them (it comes in through addRecording).
void ThumbnailStack::applyCopySave(Entry &e) {
    auto &settings = m_daemon.service<Settings>();
    Entry *entry = &e;
    QString error;
    if (settings.copyAutomatically() &&
        m_daemon.service<Clipboard>().copyPng(entry->current(), entry->captureTime, &error, entry->name).isEmpty())
        failure(QStringLiteral("Tarn could not copy the screenshot automatically"), error);
    if (settings.saveAutomatically()) {
        const QString path = m_daemon.service<Storage>().saveScreenshot(entry->current(), entry->captureTime, &error, entry->name);
        if (path.isEmpty())
            failure(QStringLiteral("Tarn could not save the screenshot automatically"), error);
        else if (!entry->historyId.isEmpty())
            m_daemon.service<History>().addSavedPath(entry->historyId, path);
        else
            entry->pendingSavedPaths << path; // the caller links the History entry right after add()
        if (!path.isEmpty())
            entry->savedPath = path;
    }
}

// (Re)starts the auto-close delay for a Thumbnail: the full delay from now. An
// older timer for the same Thumbnail (it was hidden for the Editor and revealed
// again) is made void by the new token.
void ThumbnailStack::armAutoClose(int id) {
    Entry *entry = entryFor(id);
    const double seconds = m_daemon.service<Settings>().closeThumbnailAfterSeconds();
    if (!entry || entry->kind == Recording || seconds <= 0)
        return;
    const int token = ++entry->autoCloseToken;
    entry->autoCloses = true;
    // Like the x button: nothing is saved. A Thumbnail being edited is the Editor's, so it stays;
    // during a Capture hide nothing closes (releaseCaptureHide arms the full delay again).
    QTimer::singleShot(qRound(seconds * 1000), this, [this, id, token] {
        const Entry *e = entryFor(id);
        if (e && !e->hidden && !captureHidden() && e->autoCloseToken == token)
            autoClose(id);
    });
}

CaptureHide &CaptureHide::operator=(CaptureHide &&other) noexcept {
    if (this != &other) {
        reset();
        m_stack = std::exchange(other.m_stack, nullptr);
    }
    return *this;
}

void CaptureHide::reset() {
    if (ThumbnailStack *stack = std::exchange(m_stack, nullptr))
        stack->releaseCaptureHide();
}

CaptureHide ThumbnailStack::hideForCapture() {
    if (m_captureHides++ == 0) {
        m_leaving = count() > 0;
        emit captureHiddenChanged();
    }
    return CaptureHide(this);
}

void ThumbnailStack::releaseCaptureHide() {
    if (m_captureHides <= 0 || --m_captureHides > 0)
        return;
    m_leaving = false;
    emit captureHiddenChanged();
    // The full auto-close delay again, from the moment the stack is back.
    for (const auto &e : m_entries)
        if (e.autoCloses && !e.hidden)
            armAutoClose(e.id);
}

void ThumbnailStack::waitUntilOffScreen() {
    if (captureHidden() && std::exchange(m_leaving, false))
        QCoreApplication::processEvents();
}

void ThumbnailStack::failure(const QString &summary, const QString &why) {
    m_daemon.service<Notifier>().notify(summary, why);
}

// Over the limit: the oldest Thumbnail on screen is saved (with its edits) and
// slides out. If the save fails it stays, one notification says why, and the
// stack stays over the limit until its next change.
void ThumbnailStack::enforceLimit() {
    while (count() > kMaxThumbnails) {
        Entry *oldest = nullptr;
        for (auto &e : m_entries)
            if (!e.hidden) {
                oldest = &e;
                break;
            }
        if (!autoSave(*oldest))
            return;
        closeFor(oldest->id, Overflow);
    }
}

bool ThumbnailStack::autoSave(Entry &entry) {
    settle(entry);
    if (entry.kind == Recording) {
        QString error;
        if (m_daemon.service<RecordingStore>().saveToFolder(entry.videoPath, &error).isEmpty()) {
            failure(QStringLiteral("Tarn could not save the oldest recording"), error);
            return false;
        }
        return true;
    }
    QString error;
    const QString path = m_daemon.service<Storage>().saveScreenshot(entry.current(), entry.captureTime, &error, entry.name);
    if (path.isEmpty()) {
        failure(QStringLiteral("Tarn could not save the oldest screenshot"), error);
        return false;
    }
    entry.savedPath = path;
    if (!entry.historyId.isEmpty())
        m_daemon.service<History>().addSavedPath(entry.historyId, path);
    return true;
}

QString ThumbnailStack::save(int id) {
    Entry *entry = settledEntry(id);
    if (!entry)
        return {};
    if (entry->kind == Recording) {
        QString error;
        const QString path = m_daemon.service<RecordingStore>().saveToFolder(entry->videoPath, &error);
        if (path.isEmpty()) {
            failure(QStringLiteral("Tarn could not save the recording"), error);
            return {};
        }
        closeFor(id, Saved);
        return path;
    }
    // Already saved (Save automatically) and not edited since: Save only closes.
    if (!entry->savedPath.isEmpty() && QFileInfo::exists(entry->savedPath)) {
        const QString path = entry->savedPath;
        closeFor(id, Saved);
        return path;
    }
    const QString path = m_daemon.service<History>().saveImage(entry->historyId, entry->current(), entry->captureTime, entry->name);
    if (path.isEmpty())
        return {};
    closeFor(id, Saved);
    return path;
}

QString ThumbnailStack::saveAsFolder() const {
    const QString last = m_daemon.service<RememberedState>().value(QStringLiteral("save_as.folder")).toString();
    if (!last.isEmpty() && QFileInfo(last).isDir())
        return last;
    return m_daemon.service<Storage>().screenshotDir();
}

QString ThumbnailStack::saveAsSuggestion(int id) const {
    const Entry *entry = entryFor(id);
    auto &storage = m_daemon.service<Storage>();
    const QString base = entry ? storage.baseName(entry->captureTime, entry->name) : storage.baseName(m_daemon.now(), {});
    return saveAsFolder() + '/' + base + '.' + storage.imageExtension();
}

int ThumbnailStack::saveAsFilterIndex() const { return m_daemon.service<Storage>().imageFormatIndex(); }

CaptureName ThumbnailStack::captureName(int id) const {
    const Entry *entry = entryFor(id);
    return entry ? entry->name : CaptureName{};
}

bool ThumbnailStack::saveAs(int id, const QString &path) {
    Entry *entry = settledEntry(id);
    if (!entry || entry->kind == Recording || path.isEmpty())
        return false;
    if (!m_daemon.service<History>().saveImageAs(entry->historyId, entry->current(), path))
        return false;
    closeFor(id, SavedAs);
    return true;
}

bool ThumbnailStack::copy(int id) {
    Entry *entry = settledEntry(id);
    if (!entry)
        return false;
    QString error;
    if (entry->kind == Recording) {
        // The clipboard gets a link of its own: the kept file goes once the Recording can't be restored.
        const QString shared = m_daemon.service<RecordingStore>().shareCopy(entry->videoPath, &error);
        if (shared.isEmpty() || !m_daemon.service<Clipboard>().copyFile(shared, &error)) {
            failure(QStringLiteral("Tarn could not copy the recording"), error);
            return false;
        }
    } else if (m_daemon.service<Clipboard>().copyPng(entry->current(), entry->captureTime, &error, entry->name).isEmpty()) {
        failure(QStringLiteral("Tarn could not copy the screenshot"), error);
        return false;
    }
    closeFor(id, Copied); // no notification, no sound on success
    return true;
}

ThumbnailStack::Capture ThumbnailStack::pin(int id) {
    const Entry *entry = settledEntry(id);
    if (!entry || entry->kind == Recording)
        return {};
    Capture capture{entry->png, entry->edited, entry->captureTime, entry->name, entry->historyId, entry->origin};
    closeFor(id, Pinned);
    return capture;
}

int ThumbnailStack::adopt(const Capture &capture) {
    QImage image = QImage::fromData(capture.current(), "PNG");
    if (image.isNull())
        return -1;
    Entry entry;
    entry.png = capture.png;
    entry.edited = capture.edited;
    entry.image = std::move(image);
    entry.captureTime = capture.captureTime;
    entry.name = capture.name;
    entry.historyId = capture.historyId;
    entry.origin = capture.origin;
    return insert(std::move(entry));
}

bool ThumbnailStack::holdsHistoryEntry(const QString &historyId) const {
    for (const auto &e : m_entries)
        if (e.historyId == historyId)
            return true;
    return false;
}

int ThumbnailStack::idForHistory(const QString &historyId) const {
    if (historyId.isEmpty())
        return -1;
    for (const auto &e : m_entries)
        if (e.historyId == historyId)
            return e.id;
    return -1;
}

int ThumbnailStack::restoreFromHistory(const QStringList &historyIds) {
    auto &history = m_daemon.service<History>();
    QList<Capture> chosen;
    QStringList seen;
    for (const QString &id : historyIds) {
        if (chosen.size() >= kMaxThumbnails)
            break;
        if (id.isEmpty() || seen.contains(id) || holdsHistoryEntry(id))
            continue;
        seen << id;
        Capture capture;
        capture.png = history.originalPng(id);
        if (capture.png.isEmpty())
            continue;
        capture.edited = history.editedPng(id);
        const QJsonObject description = history.description(id);
        capture.captureTime = QDateTime::fromString(description.value(QStringLiteral("captureTime")).toString(), Qt::ISODate);
        capture.name = history.captureName(id);
        capture.historyId = id;
        chosen << capture;
    }
    int restored = 0;
    for (auto it = chosen.crbegin(); it != chosen.crend(); ++it) // oldest in, newest last
        if (adopt(*it) >= 0)
            ++restored;
    return restored;
}

void ThumbnailStack::requestPin(int id) {
    const Entry *entry = entryFor(id);
    if (entry && (entry->kind == Screenshot || isGif(*entry)))
        emit pinRequested(id);
}

bool ThumbnailStack::pinnable(int id) const {
    const Entry *entry = entryFor(id);
    return entry && (entry->kind == Screenshot || isGif(*entry));
}

bool ThumbnailStack::isGif(const Entry &entry) {
    return entry.kind == Recording && entry.videoPath.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive);
}

void ThumbnailStack::setOrigin(int id, const QRect &layoutRect) {
    Entry *entry = entryFor(id);
    if (!entry)
        return;
    entry->origin = layoutRect;
    // One quick `hyprctl monitors` call, like the capture path's. A failed call leaves the stack
    // on the monitor it already follows.
    QString error;
    const auto monitor = m_daemon.service<HyprlandBridge>().monitorContaining(layoutRect.center(), &error);
    if (monitor && error.isEmpty() && monitor->name != m_monitorName) {
        m_monitorName = monitor->name;
        emit monitorChanged();
    }
}

QRect ThumbnailStack::origin(int id) const {
    const Entry *entry = entryFor(id);
    return entry ? entry->origin : QRect();
}

void ThumbnailStack::close(int id) { closeFor(id, Closed); }

bool ThumbnailStack::autoClose(int id) {
    const Entry *entry = entryFor(id);
    if (!entry || entry->kind == Recording)
        return false;
    closeFor(id, AutoClosed);
    return true;
}

bool ThumbnailStack::trash(int id) {
    const Entry *entry = entryFor(id);
    if (!entry)
        return false;
    if (entry->kind == Screenshot && (entry->historyId.isEmpty() || !m_daemon.service<History>().trash(entry->historyId))) {
        failure(QStringLiteral("Tarn could not delete the screenshot"), QStringLiteral("its original is not in History, or the Trash could not take it"));
        return false;
    }
    closeFor(id, Deleted);
    return true;
}

void ThumbnailStack::closeAll() {
    // Oldest first, so the one nearest the corner closes last and is the one `restore` brings back.
    QList<int> all;
    for (const auto &e : m_entries)
        all << e.id;
    for (int id : all)
        closeFor(id, CloseAll);
}

void ThumbnailStack::closeFor(int id, CloseReason reason) {
    int index = -1;
    for (int i = 0; i < int(m_entries.size()); ++i)
        if (m_entries[i].id == id)
            index = i;
    if (index < 0)
        return;
    settle(m_entries[index]); // the restore slot keeps a whole capture
    const int row = visibleRow(id);
    if (row >= 0)
        beginRemoveRows({}, row, row);
    Entry entry = std::move(m_entries[index]);
    m_entries.erase(m_entries.begin() + index);
    if (row >= 0)
        endRemoveRows();
    // What was in the slot can no longer be restored: a Recording's kept file goes with it.
    if (m_slot && m_slot->entry.kind == Recording) {
        m_daemon.service<RecordingStore>().discard(m_slot->entry.videoPath);
        m_daemon.service<RecordingStore>().discard(m_slot->entry.sourcePath);
    }
    m_slot = Slot{std::move(entry), reason};
    if (row >= 0)
        emit countChanged();
    emit closed(id);
}

bool ThumbnailStack::restore() {
    if (!m_slot)
        return false;
    if (m_slot->reason == Deleted && m_slot->entry.kind == Screenshot && !m_daemon.service<History>().restore(m_slot->entry.historyId)) {
        failure(QStringLiteral("Tarn could not restore the screenshot"), QStringLiteral("its original could not be taken out of the Trash"));
        return false;
    }
    Entry entry = std::move(m_slot->entry);
    m_slot.reset();
    insert(std::move(entry)); // a new id: the old card may still be sliding out
    return true;
}

void ThumbnailStack::setHidden(int id, bool hide) {
    Entry *entry = entryFor(id);
    if (!entry || entry->hidden == hide)
        return;
    if (hide) {
        const int row = visibleRow(id);
        beginRemoveRows({}, row, row);
        entry->hidden = true;
        endRemoveRows();
        emit countChanged();
        emit hidden(id);
    } else {
        entry->hidden = false; // to find the row it takes...
        const int row = visibleRow(id);
        entry->hidden = true; // ...but announce it before it counts
        beginInsertRows({}, row, row);
        entry->hidden = false;
        endInsertRows();
        emit countChanged();
        emit revealed(id);
        armAutoClose(id); // the delay starts again when the Thumbnail comes back
        enforceLimit();
    }
}

bool ThumbnailStack::isHidden(int id) const {
    const Entry *entry = entryFor(id);
    return entry && entry->hidden;
}

void ThumbnailStack::setEdited(int id, const QByteArray &png) {
    Entry *entry = settledEntry(id);
    if (entry) {
        // An earlier save no longer matches once the picture changes (an Editor
        // visit that changed nothing keeps it).
        if (entry->current() != (png.isEmpty() ? entry->png : png))
            entry->savedPath.clear();
        entry->autoFrame.reset(); // from now on the Editor's document is the truth, Frame included
    }
    if (entry && png.isEmpty()) {
        entry->edited.clear();
        entry->image = QImage::fromData(entry->png, "PNG");
        return;
    }
    const QImage image = QImage::fromData(png, "PNG");
    if (!entry || image.isNull())
        return;
    entry->edited = png;
    entry->image = image;
}

std::optional<Frame> ThumbnailStack::autoFrame(int id) const {
    const Entry *entry = entryFor(id);
    return entry ? entry->autoFrame : std::nullopt;
}

QImage ThumbnailStack::originalImage(int id) const {
    const Entry *entry = entryFor(id);
    if (!entry)
        return {};
    return entry->original.isNull() ? QImage::fromData(entry->png, "PNG") : entry->original; // no wait for the PNG
}

QByteArray ThumbnailStack::edited(int id) const {
    const Entry *entry = entryFor(id);
    return entry ? entry->edited : QByteArray();
}

QDateTime ThumbnailStack::captureTime(int id) const {
    const Entry *entry = entryFor(id);
    return entry ? entry->captureTime : QDateTime();
}

void ThumbnailStack::edit(int id) {
    const Entry *entry = entryFor(id);
    if (!entry || entry->hidden)
        return;
    if (entry->kind == Recording)
        emit trimRequested(id);
    else
        emit editRequested(id);
}

ThumbnailStack::Kind ThumbnailStack::kind(int id) const {
    const Entry *entry = entryFor(id);
    return entry ? entry->kind : Screenshot;
}

QString ThumbnailStack::chip(int id) const {
    const Entry *entry = entryFor(id);
    if (!entry || entry->kind != Recording)
        return {};
    const qint64 seconds = (entry->durationMs + 500) / 1000;
    return QStringLiteral("%1 %2:%3")
        .arg(QFileInfo(entry->videoPath).suffix().toUpper())
        .arg(seconds / 60)
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString ThumbnailStack::videoPath(int id) const {
    const Entry *entry = entryFor(id);
    return entry ? entry->videoPath : QString();
}

QString ThumbnailStack::sourcePath(int id) const {
    const Entry *entry = entryFor(id);
    return entry ? entry->sourcePath : QString();
}

qint64 ThumbnailStack::durationMs(int id) const {
    const Entry *entry = entryFor(id);
    return entry ? entry->durationMs : 0;
}

void ThumbnailStack::setRecordingTrimmed(int id, const QByteArray &posterPng, qint64 durationMs) {
    Entry *entry = entryFor(id);
    if (!entry || entry->kind != Recording)
        return;
    const QImage image = QImage::fromData(posterPng, "PNG");
    if (!image.isNull()) {
        entry->png = posterPng;
        entry->image = image;
    }
    entry->durationMs = durationMs;
}

bool ThumbnailStack::saveRecordingAs(int id, const QString &path) {
    Entry *entry = entryFor(id);
    if (!entry || entry->kind != Recording || path.isEmpty())
        return false;
    const QString summary = QStringLiteral("Tarn could not save the recording");
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        failure(summary, QStringLiteral("could not create the folder %1").arg(QFileInfo(path).absolutePath()));
        return false;
    }
    // Written under a temporary name and renamed, so a half-written file never has the final one.
    const QString partial = path + QStringLiteral(".part");
    QFile::remove(partial);
    if (!QFile::copy(entry->videoPath, partial) || (QFile::exists(path) && !QFile::remove(path)) || !QFile::rename(partial, path)) {
        QFile::remove(partial);
        failure(summary, QStringLiteral("could not write %1").arg(path));
        return false;
    }
    m_daemon.service<RememberedState>().setValue(QStringLiteral("save_as.folder"), QFileInfo(path).absolutePath());
    closeFor(id, SavedAs);
    return true;
}

void ThumbnailStack::saveAutoProject(const QString &historyId, const QImage &image, bool framed) {
    if (!framed || historyId.isEmpty())
        return;
    const auto frame = m_daemon.service<PresetStore>().autoFrame();
    if (!frame || image.isNull())
        return;
    CaptureDocument document(image);
    document.setFrameLive(*frame);
    m_daemon.service<History>().saveProject(historyId, document);
}

void ThumbnailStack::setHistoryId(int id, const QString &historyId, bool writeProject) {
    Entry *entry = entryFor(id);
    if (!entry)
        return;
    entry->historyId = historyId;
    for (const QString &path : std::exchange(entry->pendingSavedPaths, {}))
        m_daemon.service<History>().addSavedPath(historyId, path);
    // The auto-applied Preset is the capture's first edit: History hands out the
    // framed shot the Thumbnail shows, and the Editor reopens with the Frame.
    if (entry->autoFrame && writeProject) {
        CaptureDocument document(originalImage(id));
        document.setFrameLive(*entry->autoFrame);
        m_daemon.service<History>().saveProject(historyId, document);
    }
}

QString ThumbnailStack::historyId(int id) const {
    const Entry *entry = entryFor(id);
    return entry ? entry->historyId : QString();
}

QImage ThumbnailStack::image(int id) const {
    const Entry *entry = entryFor(id);
    return entry ? entry->image : QImage();
}

QList<int> ThumbnailStack::ids() const {
    QList<int> out;
    for (const auto &e : m_entries)
        if (!e.hidden)
            out << e.id;
    return out;
}

QVariant ThumbnailStack::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= count())
        return {};
    const Entry *entry = entryFor(ids().at(index.row()));
    if (role == IdRole)
        return entry->id;
    if (role == KindRole)
        return int(entry->kind);
    return {};
}

ThumbnailStack::Entry *ThumbnailStack::entryFor(int id) {
    for (auto &e : m_entries)
        if (e.id == id)
            return &e;
    return nullptr;
}

const ThumbnailStack::Entry *ThumbnailStack::entryFor(int id) const {
    return const_cast<ThumbnailStack *>(this)->entryFor(id);
}

int ThumbnailStack::visibleRow(int id) const {
    int row = 0;
    for (const auto &e : m_entries) {
        if (e.id == id)
            return e.hidden ? -1 : row;
        row += e.hidden ? 0 : 1;
    }
    return -1;
}

} // namespace tarn
