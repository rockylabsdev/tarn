#pragma once
// The Overlay: opens on a frozen image of one monitor, owns the Overlay model,
// and turns a confirmed Selection into a capture. The layer surface that draws
// it (src/ui/overlay_surface) only listens: it shows on opened(), hides on
// closed(), and reports back with surfaceGone() once it is off screen.
#include "overlay_model.h"
#include "recording/record_options.h"
#include "storage/file_name.h"
#include "thumbnails/thumbnail_stack.h"

#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QString>
#include <QList>
#include <QByteArray>

namespace tarn {

class Daemon;
struct Reply;

class Overlay : public QObject {
    Q_OBJECT
    Q_PROPERTY(tarn::OverlayModel *model READ modelPtr CONSTANT)
    Q_PROPERTY(tarn::RecordOptions *recordOptions READ recordOptions CONSTANT)
    Q_PROPERTY(int generation READ generation NOTIFY opened)
public:
    explicit Overlay(Daemon &daemon);

    // Print: hides the Thumbnails (Capture hide, until the Overlay closes), grabs
    // the frozen image (the Overlay is not mapped yet), then opens. Opening while
    // open, or while that grab runs (a second key press), does nothing. On failure notifies and returns the error.
    Reply open(OverlayModel::Mode mode, bool timer = false);

    OverlayModel &model() { return m_model; }
    OverlayModel *modelPtr() { return &m_model; }
    // The Record panel's toggles and its settings button.
    RecordOptions *recordOptions();
    // Closes the Overlay and opens config.toml in the user's editor.
    Q_INVOKABLE void openSettings();
    bool isOpen() const { return m_model.active(); }

    // The frozen image (full resolution, as grim made it), its monitor's name
    // and place in the layout, and a counter QML puts in the image URL.
    QImage frozen() const { return m_frozen; }
    QString monitorName() const { return m_monitorName; }
    QRect monitorRect() const { return m_monitorRect; }
    int generation() const { return m_generation; }

    // The part of a full-resolution image of the monitor under `selection`
    // (logical px of the monitor).
    static QImage cropSelection(const QImage &frozen, const QRect &monitorRect, const QRect &selection);

    // Crops `selection` (logical px of the monitor) out of a full-resolution
    // image of that monitor, shows it as a Thumbnail and records it in History
    // (kind "fullscreen" when the Selection is the whole monitor, else "area").
    // Used by a confirmed Selection and by Shift+Print (capture repeat).
    void takeArea(const QImage &frozen, const QRect &monitorRect, const QRect &selection, const QDateTime &captureTime,
                  bool framed = true);

    // A surface says it exists, so closing is only finished once it reports
    // surfaceGone(). With no surface (tests, headless) closing is instant.
    void attachSurface() { m_hasSurface = true; }
    void surfaceGone();
    bool isClosing() const { return m_closing; }
    // Blocks (running the event loop) until a closing Overlay is off screen, at
    // most `timeoutMs`. Returns at once if it is not closing.
    void waitUntilOffScreen(int timeoutMs = 500);

signals:
    void opened();
    void closed();   // the surface should hide now
    void offScreen(); // it has

private:
    void onConfirmed(const QRect &selection, OverlayModel::Mode mode);
    void onConfirmedWindow(const WindowInfo &window);
    void onCanceled();
    void onColorPicked(const QColor &color, bool asRgb);
    void close();
    // A finished shot: onto the stack at once, or (Sequence) held in order until close().
    void deliver(const QByteArray &png, const QDateTime &captureTime, const CaptureName &name, bool framed, const QString &historyId,
                 const QRect &origin);
    void pushHeld();
    void remember(const QRect &selection);
    void restoreLastArea();

    Daemon &m_daemon;
    OverlayModel m_model;
    QImage m_frozen;
    QString m_monitorName;
    QRect m_monitorRect;
    int m_generation = 0;
    bool m_hasSurface = false;
    bool m_closing = false;
    // The Thumbnails stay off screen while the Overlay is open; a confirmed Selection hands
    // this on (the Countdown and the Recording take their own before it is released).
    CaptureHide m_hide;
    struct Held {
        QByteArray png;
        QDateTime captureTime;
        CaptureName name;
        bool framed;
        QString historyId;
        QRect origin;
    };
    QList<Held> m_held; // Sequence shots whose Thumbnails wait for the Overlay to close
};

} // namespace tarn
