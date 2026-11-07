#include "pins.h"

#include "clipboard/clipboard.h"
#include "daemon/daemon.h"
#include "history/history.h"
#include "hyprland/hyprland_bridge.h"
#include "notify/notifier.h"
#include "recording/recording_store.h"
#include "state/remembered_state.h"
#include "storage/image_file.h"
#include "storage/storage.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>
#include <QtMath>
#include <algorithm>

namespace tarn {

Pins::Pins(Daemon &daemon) : m_daemon(daemon) {
    QDir(gifCacheDir()).removeRecursively(); // GIF copies a crash left behind
    connect(&stack(), &ThumbnailStack::pinRequested, this, &Pins::pin);
}

Pins::~Pins() {
    for (const auto &[id, p] : m_pins)
        if (!p.gifPath.isEmpty())
            QDir(QFileInfo(p.gifPath).absolutePath()).removeRecursively();
}

QString Pins::gifCacheDir() {
    QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (cache.isEmpty())
        cache = QDir::homePath() + QStringLiteral("/.cache");
    return cache + QStringLiteral("/tarn/pins");
}

bool Pins::isGif(int pin) const {
    const Pin *p = find(pin);
    return p && !p->gifPath.isEmpty();
}

QString Pins::gifPath(int pin) const {
    const Pin *p = find(pin);
    return p ? p->gifPath : QString();
}

ThumbnailStack &Pins::stack() { return m_daemon.service<ThumbnailStack>(); }

Pins::Pin *Pins::find(int pin) {
    const auto it = m_pins.find(pin);
    return it == m_pins.end() ? nullptr : &it->second;
}

const Pins::Pin *Pins::find(int pin) const { return const_cast<Pins *>(this)->find(pin); }

QString Pins::title(int pin) { return QStringLiteral("Tarn pin %1").arg(pin); }

QList<int> Pins::ids() const {
    QList<int> out;
    for (const auto &[id, p] : m_pins)
        out << id;
    return out;
}

QImage Pins::image(int pin) const {
    const Pin *p = find(pin);
    return p ? p->image : QImage();
}

QByteArray Pins::png(int pin) const {
    const Pin *p = find(pin);
    return p ? p->capture.current() : QByteArray();
}

QRect Pins::openingRect(int pin) const {
    const Pin *p = find(pin);
    return p ? p->opening : QRect();
}

qreal Pins::opacity(int pin) const {
    const Pin *p = find(pin);
    return p ? p->opacity : 1.0;
}

// A GIF Pin: the Thumbnail stays, the Pin gets a copy of the file in a folder of its own.
int Pins::pinGif(int recordingId) {
    ThumbnailStack &s = stack();
    const QString source = s.videoPath(recordingId);
    const QImage poster = s.image(recordingId);
    if (poster.isNull() || !QFileInfo::exists(source))
        return -1;
    const int id = m_nextId;
    const QString dir = QStringLiteral("%1/pin-%2").arg(gifCacheDir()).arg(id);
    const QString name = QFileInfo(source).fileName();
    const QString path = dir + '/' + name;
    if (!QDir().mkpath(dir) || !QFile::copy(source, path)) {
        QDir(dir).removeRecursively();
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not pin the GIF"), QStringLiteral("could not copy %1").arg(name));
        return -1;
    }
    Pin p;
    p.image = poster; // the picture's shape; the window plays the GIF
    p.gifPath = path;
    p.capture.captureTime = s.captureTime(recordingId); // Save as names the GIF like the Recording
    p.capture.name = s.captureName(recordingId);
    QPoint topLeft;
    QSize size = poster.size();
    const QRect origin = s.origin(recordingId);
    if (origin.isValid()) {
        topLeft = origin.topLeft();
        size = origin.size();
    } else {
        QString error;
        const auto monitor = m_daemon.service<HyprlandBridge>().monitorUnderPointer(&error);
        if (monitor)
            topLeft = (QRectF(monitor->logicalRect).center() - QPointF(size.width() / 2.0, size.height() / 2.0)).toPoint();
    }
    p.opening = QRect(topLeft, size);
    p.rect = p.opening;
    ++m_nextId;
    QString error;
    if (!m_daemon.service<HyprlandBridge>().openWindowAt(title(id), p.opening, &error))
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not place the Pin"), error);
    m_pins.emplace(id, std::move(p));
    emit opened(id);
    return id;
}

int Pins::pin(int thumbnailId) {
    if (stack().kind(thumbnailId) == ThumbnailStack::Recording)
        return stack().pinnable(thumbnailId) ? pinGif(thumbnailId) : -1;
    ThumbnailStack::Capture capture = stack().pin(thumbnailId);
    if (capture.isNull())
        return -1;
    return pinCapture(capture);
}

int Pins::pinCapture(const ThumbnailStack::Capture &capture) {
    if (capture.isNull())
        return -1;
    Pin p;
    p.image = QImage::fromData(capture.current(), "PNG");
    if (p.image.isNull())
        return -1;

    // Where it was captured, at the size it was captured: the image's pixels over
    // the capture's scale (the screenshot's width over the logical width it covered),
    // so an edited image keeps its pixel density.
    double density = 1.0;
    QPoint topLeft;
    if (capture.origin.isValid()) {
        const QImage original = QImage::fromData(capture.png, "PNG");
        if (!original.isNull())
            density = qreal(original.width()) / capture.origin.width();
        topLeft = capture.origin.topLeft();
    } else {
        QString error;
        const auto monitor = m_daemon.service<HyprlandBridge>().monitorUnderPointer(&error);
        if (monitor) {
            // No place of its own (a History entry): its pixels are shown at 1:1, which on a
            // scaled monitor is its width over the scale in logical pixels.
            density = monitor->scale;
            topLeft = (QRectF(monitor->logicalRect).center() - QPointF(p.image.width() / density / 2.0, p.image.height() / density / 2.0)).toPoint();
        }
    }
    p.opening = QRect(topLeft, QSize(qMax(1, qRound(p.image.width() / density)), qMax(1, qRound(p.image.height() / density))));
    p.rect = p.opening;
    p.capture = capture;

    const int id = m_nextId++;
    QString error;
    if (!m_daemon.service<HyprlandBridge>().openWindowAt(title(id), p.opening, &error))
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not place the Pin"), error);
    m_pins.emplace(id, std::move(p));
    emit opened(id);
    return id;
}

void Pins::changeOpacity(int pin, qreal angleDeltaY) {
    Pin *p = find(pin);
    if (!p)
        return;
    const qreal now = std::clamp(p->opacity + angleDeltaY / 120.0 * kOpacityStep, kMinOpacity, 1.0);
    if (qFuzzyCompare(now, p->opacity))
        return;
    p->opacity = now;
    emit opacityChanged(pin);
}

// Scroll notches arrive in a rapid run. Each only moves the wanted width, tracked
// here; one hyprctl call per run places the window. The window is looked up once
// at the start of a run (the user may have dragged it since the last one).
void Pins::resize(int pin, qreal angleDeltaY) {
    Pin *p = find(pin);
    if (!p)
        return;
    if (!p->resizePending) {
        QString error;
        if (const auto now = m_daemon.service<HyprlandBridge>().windowRect(title(pin), &error))
            p->rect = *now;
        p->wantedWidth = p->rect.width();
        // A window left off-shape (by a mouse drag) is first brought to the shape.
        p->resizePending = true;
        QTimer::singleShot(kResizeCoalesceMs, this, [this, pin] { applyResize(pin); });
    }
    const qreal factor = std::clamp(qPow(kScrollFactor, angleDeltaY / 120.0), 0.01, 100.0);
    const qreal base = p->opening.width();
    p->wantedWidth = std::clamp(p->wantedWidth * factor, base * kMinScale, base * kMaxScale);
}

// The picture's own shape: width over height of the image, not of the opening
// rectangle (which is rounded to whole logical pixels).
QSize Pins::shapedSize(const Pin &p, qreal width) {
    const qreal ratio = qreal(p.image.height()) / p.image.width();
    return QSize(qMax(1, qRound(width)), qMax(1, qRound(qMax(1, qRound(width)) * ratio)));
}

bool Pins::isOnShape(const Pin &p, const QSize &size) {
    return qAbs(shapedSize(p, size.width()).height() - size.height()) <= 1;
}

// Something other than a scroll changed the window's size (Hyprland's own mouse
// resize, say). If it is no longer the picture's shape, put the height back to
// match the width. Events are gathered for a moment so a drag costs one call.
void Pins::windowResized(int pin) {
    Pin *p = find(pin);
    if (!p || p->resizePending || p->snapPending)
        return;
    p->snapPending = true;
    QTimer::singleShot(kResizeCoalesceMs, this, [this, pin] { snapToShape(pin); });
}

void Pins::snapToShape(int pin) {
    Pin *p = find(pin);
    if (!p)
        return;
    p->snapPending = false;
    if (p->resizePending)
        return;
    QString error;
    const auto now = m_daemon.service<HyprlandBridge>().windowRect(title(pin), &error);
    if (!now)
        return;
    p->rect = *now;
    if (isOnShape(*p, now->size()))
        return;
    const QRect target(now->topLeft(), shapedSize(*p, now->width()));
    if (m_daemon.service<HyprlandBridge>().setWindowRect(title(pin), target, &error))
        p->rect = target;
}

void Pins::applyResize(int pin) {
    Pin *p = find(pin);
    if (!p)
        return;
    p->resizePending = false;
    const QSize size = shapedSize(*p, p->wantedWidth);
    if (size == p->rect.size())
        return;
    const QPointF center = QRectF(p->rect).center();
    const QRect target(QPoint(qRound(center.x() - size.width() / 2.0), qRound(center.y() - size.height() / 2.0)), size);
    QString error;
    if (m_daemon.service<HyprlandBridge>().setWindowRect(title(pin), target, &error))
        p->rect = target;
}

bool Pins::copy(int pin) {
    const Pin *p = find(pin);
    if (!p)
        return false;
    QString error;
    if (!p->gifPath.isEmpty()) {
        // The clipboard gets a link of its own: what was copied outlives the Pin's file.
        const QString shared = m_daemon.service<RecordingStore>().shareCopy(p->gifPath, &error);
        if (shared.isEmpty() || !m_daemon.service<Clipboard>().copyFile(shared, &error)) {
            m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not copy the GIF"), error);
            return false;
        }
        return true;
    }
    if (m_daemon.service<Clipboard>().copyPng(p->capture.current(), p->capture.captureTime, &error, p->capture.name).isEmpty()) {
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not copy the screenshot"), error);
        return false;
    }
    return true;
}

QString Pins::saveAsFolder() const { return m_daemon.service<ThumbnailStack>().saveAsFolder(); }

QString Pins::saveAsSuggestion(int pin) const {
    const Pin *p = find(pin);
    auto &storage = m_daemon.service<Storage>();
    const QString base = p ? storage.baseName(p->capture.captureTime, p->capture.name) : storage.baseName(m_daemon.now(), {});
    const QString ext = p && !p->gifPath.isEmpty() ? QStringLiteral("gif") : storage.imageExtension();
    return saveAsFolder() + '/' + base + '.' + ext;
}

int Pins::saveAsFilterIndex() const { return m_daemon.service<Storage>().imageFormatIndex(); }

bool Pins::saveAs(int pin, const QString &path) {
    const Pin *p = find(pin);
    if (!p || path.isEmpty())
        return false;
    QString error;
    if (!p->gifPath.isEmpty()) {
        const QString summary = QStringLiteral("Tarn could not save the GIF");
        if (!path.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive)) {
            m_daemon.service<Notifier>().notify(summary, QStringLiteral("a GIF is saved as .gif"));
            return false;
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        const QString partial = path + QStringLiteral(".part");
        QFile::remove(partial);
        if (!QFile::copy(p->gifPath, partial) || (QFile::exists(path) && !QFile::remove(path)) || !QFile::rename(partial, path)) {
            QFile::remove(partial);
            m_daemon.service<Notifier>().notify(summary, QStringLiteral("could not write %1").arg(path));
            return false;
        }
        m_daemon.service<RememberedState>().setValue(QStringLiteral("save_as.folder"), QFileInfo(path).absolutePath());
        return true;
    }
    return m_daemon.service<History>().saveImageAs(p->capture.historyId, p->capture.current(), path);
}

void Pins::edit(int pin) {
    Pin *p = find(pin);
    if (!p || !p->gifPath.isEmpty())
        return;
    const ThumbnailStack::Capture capture = p->capture;
    close(pin);
    const int id = stack().adopt(capture);
    if (id >= 0)
        stack().edit(id);
}

bool Pins::isLocked(int pin) const {
    const Pin *p = find(pin);
    return p && p->locked;
}

bool Pins::setLocked(int pin, bool locked) {
    Pin *p = find(pin);
    if (!p)
        return false;
    if (p->locked == locked)
        return true;
    QString error;
    if (!m_daemon.service<HyprlandBridge>().setWindowNoFocus(title(pin), locked, &error)) {
        m_daemon.service<Notifier>().notify(locked ? QStringLiteral("Tarn could not lock the Pin") : QStringLiteral("Tarn could not unlock the Pin"), error);
        return false;
    }
    p->locked = locked;
    emit lockedChanged(pin);
    return true;
}

bool Pins::lock(int pin) { return setLocked(pin, true); }
bool Pins::unlock(int pin) { return setLocked(pin, false); }

bool Pins::lockToggle() {
    const QList<int> all = ids();
    const bool lockAll = std::any_of(all.begin(), all.end(), [this](int id) { return !isLocked(id); });
    bool ok = true;
    for (int id : all)
        ok = setLocked(id, lockAll) && ok;
    return ok;
}

void Pins::close(int pin) {
    const Pin *p = find(pin);
    if (!p)
        return;
    if (!p->gifPath.isEmpty())
        QDir(QFileInfo(p->gifPath).absolutePath()).removeRecursively();
    m_pins.erase(pin);
    emit closed(pin);
}

void Pins::closeAll() {
    for (int id : ids())
        close(id);
}

TARN_MODULE(pin, [](Daemon &daemon) {
    daemon.service<Pins>(); // listens for Pin on a Thumbnail from the start
});

} // namespace tarn
