#include "overlay.h"

#include "clipboard/clipboard.h"
#include "last_area.h"
#include "daemon/daemon.h"
#include "timer/countdown.h"
#include "grab/screen_grabber.h"
#include "history/history.h"
#include "hyprland/hyprland_bridge.h"
#include "notify/notifier.h"
#include "preset/preset_store.h"
#include "recording/record_options.h"
#include "recording/recording_session.h"
#include "saved_colors/saved_colors.h"
#include "settings/settings.h"
#include "storage/png_encoder.h"
#include "state/remembered_state.h"
#include "text/text_capture.h"
#include "thumbnails/thumbnail_stack.h"
#include "window_shot.h"

#include <QEventLoop>
#include <QTimer>

namespace tarn {

Overlay::Overlay(Daemon &daemon) : m_daemon(daemon) {
    connect(&m_model, &OverlayModel::confirmed, this, &Overlay::onConfirmed);
    connect(&m_model, &OverlayModel::canceled, this, &Overlay::onCanceled);
    connect(&m_model, &OverlayModel::colorPicked, this, &Overlay::onColorPicked);
    // The Magnifier stays as the user left it: on at the next capture, and after a restart.
    m_model.setMagnifier(m_daemon.service<RememberedState>().value(QStringLiteral("magnifier.on"), false).toBool());
    connect(&m_model, &OverlayModel::magnifierToggled, this, [this](bool on) {
        m_daemon.service<RememberedState>().setValue(QStringLiteral("magnifier.on"), on);
    });
    // The Text layout stays as the user left it too.
    m_model.setTextLayout(textLayoutFromKey(m_daemon.service<RememberedState>().value(QStringLiteral("text.layout"), QString()).toString()));
    connect(&m_model, &OverlayModel::textLayoutToggled, this, [this](TextLayout layout) {
        m_daemon.service<RememberedState>().setValue(QStringLiteral("text.layout"), textLayoutKey(layout));
    });
    connect(&m_model, &OverlayModel::pointerWarpRequested, this, [this](const QPoint &p) {
        m_daemon.service<HyprlandBridge>().takeSnapshot(); // before the Overlay is on screen
    QString error;
        m_daemon.service<HyprlandBridge>().warpPointer(p + m_monitorRect.topLeft(), &error);
    });

    connect(&m_model, &OverlayModel::confirmedWindow, this, &Overlay::onConfirmedWindow);
    connect(&m_model, &OverlayModel::lastAreaRequested, this, &Overlay::restoreLastArea);
}

// L: the focused app's Last area, back as a kept Selection to adjust. Nothing
// happens when the app has none, or it no longer lies on this monitor.
void Overlay::restoreLastArea() {
    const auto focused = m_daemon.service<HyprlandBridge>().focusedWindow();
    if (!focused)
        return;
    const QRect area = lastarea::recall(m_daemon.service<RememberedState>(), focused->app);
    const QRect local = area.translated(-m_monitorRect.topLeft()).intersected(QRect(QPoint(0, 0), m_monitorRect.size()));
    if (!area.isEmpty() && !local.isEmpty())
        m_model.keepArea(local);
}

Reply Overlay::open(OverlayModel::Mode mode, bool timer) {
    if (isOpen() || m_daemon.service<ScreenGrabber>().busy())
        return Reply::success();
    m_daemon.service<HyprlandBridge>().takeSnapshot(); // before the Overlay is on screen
    QString error;
    const auto monitor = m_daemon.service<HyprlandBridge>().monitorUnderPointer(&error);
    CaptureHide hide = m_daemon.service<ThumbnailStack>().hideForCapture(); // out of the frozen image
    QImage image;
    if (monitor)
        image = m_daemon.service<ScreenGrabber>().grabMonitor(monitor->name, &error);
    if (image.isNull()) {
        if (error.isEmpty())
            error = QStringLiteral("the screenshot could not be read");
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not open the screenshot overlay"), error);
        return Reply::failure(error);
    }
    m_frozen = std::move(image);
    m_hide = std::move(hide);
    m_monitorName = monitor->name;
    m_monitorRect = monitor->logicalRect;
    m_held.clear();
    ++m_generation;
    const qreal scale = qreal(m_frozen.width()) / qMax(1, m_monitorRect.width());
    m_model.open(mode, m_monitorRect.size(), scale, timer);
    m_model.setPixelSource(m_frozen); // Color Mode reads its pixels from the frozen image
    m_model.setTimerDelay(m_daemon.service<Settings>().timerDelaySeconds());
    m_model.setPresetAvailable(m_daemon.service<PresetStore>().hasAuto()); // B can skip it
    // The window highlight works in Screenshot and Text (and any Mode switched to).
    // Not being able to list windows is not an error; the Overlay then just picks areas.
    QString windowError;
    m_model.setWindows(m_daemon.service<HyprlandBridge>().windows(*monitor, &windowError));
    if (mode == OverlayModel::Record)
        recordOptions()->refreshPointer(); // the panel grays the pointer note by it
    emit opened();
    return Reply::success();
}

RecordOptions *Overlay::recordOptions() {
    return &m_daemon.service<RecordOptions>();
}

void Overlay::openSettings() {
    QString error;
    if (!m_daemon.service<Settings>().openInEditor(&error))
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not open the settings"), error);
    m_model.cancel();
}

void Overlay::close() {
    m_closing = m_hasSurface;
    emit closed();
    pushHeld();
    m_hide.reset(); // the Thumbnails come back, a Sequence's held ones among them
}

void Overlay::pushHeld() {
    auto &stack = m_daemon.service<ThumbnailStack>();
    const QList<Held> held = std::exchange(m_held, {});
    for (const Held &shot : held) { // in order; the stack's overflow rule applies as they arrive
        const int id = stack.add(shot.image, shot.png, shot.captureTime, shot.name, shot.framed);
        if (id < 0)
            continue;
        stack.setHistoryId(id, shot.historyId, false); // its Project file was written in deliver()
        stack.setOrigin(id, shot.origin);
    }
}

void Overlay::deliver(const QImage &image, const QFuture<QByteArray> &png, const QDateTime &captureTime, const CaptureName &name,
                      bool framed, const QString &historyId, const QRect &origin) {
    if (m_model.active() && m_model.sequence()) { // Sequence: in History now, on screen when we leave
        // The Preset is saved at capture time, not when the Overlay closes.
        m_daemon.service<ThumbnailStack>().saveAutoProject(historyId, image, framed);
        m_held.append({image, png, captureTime, name, framed, historyId, origin});
        return;
    }
    auto &stack = m_daemon.service<ThumbnailStack>();
    const int id = stack.add(image, png, captureTime, name, framed);
    if (id < 0)
        return;
    stack.setHistoryId(id, historyId);
    stack.setOrigin(id, origin);
}

void Overlay::surfaceGone() {
    if (!m_closing)
        return;
    m_closing = false;
    emit offScreen();
}

void Overlay::waitUntilOffScreen(int timeoutMs) {
    if (!m_closing)
        return;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(this, &Overlay::offScreen, &loop, &QEventLoop::quit);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(timeoutMs);
    loop.exec();
    m_closing = false; // gave up waiting: do not make every later grab wait again
}

void Overlay::onCanceled() {
    close();
}

// Color pick: the hex (or rgb with Shift) goes to the clipboard, the color
// to the front of Saved colors, one notification says so, and the Overlay closes.
void Overlay::onColorPicked(const QColor &color, bool asRgb) {
    if (!m_model.active()) // a Sequence keeps the Overlay open
        close();
    const QString text = OverlayModel::colorText(color, asRgb);
    m_daemon.service<SavedColors>().save(color); // every pick is kept, even if the copy fails
    auto &notifier = m_daemon.service<Notifier>();
    QString error;
    if (!m_daemon.service<Clipboard>().copyText(text, &error)) {
        notifier.notify(QStringLiteral("Tarn could not copy the color"), error.simplified());
        return;
    }
    notifier.notify(QStringLiteral("Copied %1 · saved").arg(text));
}

// A click on a highlighted window: cut it out of the frozen image (so a menu
// open over it is in), with transparent rounded corners.
void Overlay::onConfirmedWindow(const WindowInfo &window) {
    const QDateTime captureTime = m_daemon.now();
    const bool framed = !m_model.noFrame();
    CaptureHide handoff; // the new Thumbnail is on the stack before the stack comes back
    if (!m_model.active()) {
        handoff = std::move(m_hide);
        close();
    }
    if (m_model.mode() == OverlayModel::Text) { // Text capture of a window: read its pixels, copy the text
        m_daemon.service<TextCapture>().capture(cropSelection(m_frozen, m_monitorRect, window.rect));
        return;
    }
    const qreal f = qreal(m_frozen.width()) / qMax(1, m_monitorRect.width());
    const QImage cut = cutWindow(m_frozen, window, f);
    if (cut.isNull()) {
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not take the screenshot"),
                                            QStringLiteral("the window could not be cut out"));
        return;
    }
    const QFuture<QByteArray> png = encodePngInBackground(cut); // off the daemon's thread
    const QRect global = window.rect.translated(m_monitorRect.topLeft());
    const QString historyId = m_daemon.service<History>().record(
        png, {captureTime, QStringLiteral("window"), window.app, window.title, global});
    deliver(cut, png, captureTime, {QStringLiteral("window"), window.app, window.title}, framed, historyId, global);
}

void Overlay::onConfirmed(const QRect &selection, OverlayModel::Mode mode) {
    const QDateTime captureTime = m_daemon.now();
    const bool framed = !m_model.noFrame(); // B skipped the auto-applied Preset
    // Kept until the end: the Countdown or the Recording takes its own hide first, and a
    // new Thumbnail is on the stack before the stack comes back.
    CaptureHide handoff;
    if (!m_model.active()) { // a Sequence keeps the Overlay open
        handoff = std::move(m_hide);
        close();
    }
    if (mode == OverlayModel::Screenshot && m_model.timer()) { // the Countdown takes the shot when it ends
        m_daemon.service<Countdown>().start(m_monitorName, m_monitorRect, selection, framed);
        return;
    }
    if (mode == OverlayModel::Text) { // Text capture: nothing is remembered, saved or put in History
        m_daemon.service<TextCapture>().capture(cropSelection(m_frozen, m_monitorRect, selection));
        return;
    }
    if (mode == OverlayModel::Record) {
        // The recorder films the live screen: the Overlay must be gone first.
        waitUntilOffScreen();
        m_daemon.service<RecordingSession>().start(selection, m_monitorRect, m_model.pixelScale(), m_monitorName,
                                                         m_model.confirmedAsGif() ? RecordingSession::Gif : RecordingSession::Mp4);
        return;
    }
    if (mode != OverlayModel::Screenshot)
        return; // Color picks through colorPicked(); Screenshot is the Mode that remembers and takes a shot here

    remember(selection);
    takeArea(m_frozen, m_monitorRect, selection, captureTime, framed);
}

void Overlay::remember(const QRect &selection) {
    // Filed under the app whose window the area overlaps most (not the focused one).
    const QRect global = selection.translated(m_monitorRect.topLeft());
    if (const auto window = m_daemon.service<HyprlandBridge>().windowOverlapping(global))
        lastarea::remember(m_daemon.service<RememberedState>(), window->app, global);
}

QImage Overlay::cropSelection(const QImage &frozen, const QRect &monitorRect, const QRect &selection) {
    // The Selection is in logical pixels; the crop is cut from grim's
    // full-resolution file.
    const qreal f = qreal(frozen.width()) / qMax(1, monitorRect.width());
    const int x1 = qRound(selection.left() * f), y1 = qRound(selection.top() * f);
    const int x2 = qRound((selection.left() + selection.width()) * f), y2 = qRound((selection.top() + selection.height()) * f);
    const QRect crop(x1, y1, x2 - x1, y2 - y1);
    return frozen.copy(crop.intersected(frozen.rect()));
}

void Overlay::takeArea(const QImage &frozen, const QRect &monitorRect, const QRect &selection, const QDateTime &captureTime,
                       bool framed) {
    const QImage cropped = cropSelection(frozen, monitorRect, selection);
    if (cropped.isNull()) {
        const QString error = QStringLiteral("the selected area could not be cut out");
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not take the screenshot"), error);
        return;
    }
    const QFuture<QByteArray> png = encodePngInBackground(cropped); // off the daemon's thread
    // Which window it came from, by the snapshot taken before the Overlay opened:
    // the one the area overlaps most, or for the whole monitor the focused one.
    auto &bridge = m_daemon.service<HyprlandBridge>();
    const QRect global = selection.translated(monitorRect.topLeft());
    const bool wholeMonitor = selection.contains(QRect(QPoint(0, 0), monitorRect.size()));
    const auto window = wholeMonitor ? bridge.focusedWindow() : bridge.windowOverlapping(global);
    const CaptureName name{wholeMonitor ? QStringLiteral("screen") : QStringLiteral("area"), window ? window->app : QString(),
                           window ? window->title : QString()};
    const QString historyId = m_daemon.service<History>().record(
        png, {captureTime, wholeMonitor ? QStringLiteral("fullscreen") : QStringLiteral("area"), window ? window->app : QString(), window ? window->title : QString(), global});
    deliver(cropped, png, captureTime, name, framed, historyId, global);
}

} // namespace tarn
