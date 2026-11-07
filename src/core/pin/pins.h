#pragma once
// Pin: a capture floated above every window until it is closed. This is the
// core of it (what a Pin is, and everything the user does to one); the QML
// window is a thin layer (src/ui/pin_windows.*).
//
// A Pin is an ordinary floating window titled "Tarn pin <id>". The static
// window rule in tarn.lua floats it, pins it to every workspace (Hyprland draws
// a pinned floating window above every other window) and drops the animation.
// Where it opens is a per-window rule added just before it shows; moving it is
// the compositor's (startSystemMove), resizing it is a hyprctl call, since
// Hyprland ignores a client's own resize.
#include "thumbnails/thumbnail_stack.h"

#include <QImage>
#include <QObject>
#include <QRect>
#include <map>

namespace tarn {

class Daemon;

class Pins : public QObject {
    Q_OBJECT
public:
    static constexpr qreal kMinOpacity = 0.1;
    static constexpr qreal kOpacityStep = 0.05; // per scroll notch (120)
    static constexpr qreal kMinScale = 0.1;     // of the Pin's opening size
    static constexpr qreal kMaxScale = 8.0;
    static constexpr qreal kScrollFactor = 1.1; // per scroll notch (120)
    static constexpr int kResizeCoalesceMs = 30; // notches within this are one resize

    explicit Pins(Daemon &daemon);
    ~Pins() override;

    // Pins a Thumbnail (it leaves the stack, the Pin appears where it was
    // captured). Returns the Pin's id, or -1 for an MP4 Recording or an unknown id.
    // A GIF Recording's Thumbnail stays; the Pin plays its own copy of the GIF
    // (a GIF Pin), where the GIF was recorded, at the size it was recorded.
    int pin(int thumbnailId);
    // A fresh Pin of a capture that is not on the stack (the History window's Pin):
    // always a new one, centered on the monitor under the pointer. -1 if the image is unreadable.
    int pinCapture(const ThumbnailStack::Capture &capture);

    // A GIF Pin loops a GIF with no controls. Its file is the Pin's own copy in the
    // cache (gone when the Pin closes), so Trim or Delete of the Recording cannot
    // break it. Empty for an image Pin. image() is its first frame's shape.
    Q_INVOKABLE bool isGif(int pin) const;
    Q_INVOKABLE QString gifPath(int pin) const;

    QList<int> ids() const;
    bool has(int pin) const { return m_pins.count(pin) > 0; }
    static QString title(int pin);
    QImage image(int pin) const;
    QByteArray png(int pin) const;
    // Where and how big the Pin opens (layout, logical px).
    QRect openingRect(int pin) const;
    Q_INVOKABLE qreal opacity(int pin) const;

    // Ctrl+scroll: opacity moves by a step per notch, within kMinOpacity..1.
    Q_INVOKABLE void changeOpacity(int pin, qreal angleDeltaY);
    // Scroll: resizes about the Pin's center, a tenth per notch, keeping its aspect.
    // Notches within kResizeCoalesceMs become one hyprctl call.
    Q_INVOKABLE void resize(int pin, qreal angleDeltaY);

    // The window's size changed by some other means than scroll: snap it back to the picture's shape.
    Q_INVOKABLE void windowResized(int pin);

    // Copy: the same clipboard path as a Thumbnail's Copy. The Pin stays.
    Q_INVOKABLE bool copy(int pin);
    // Save as…: PNG / JPEG / WebP by extension, in the last folder used. The Pin stays.
    Q_INVOKABLE bool saveAs(int pin, const QString &path);
    Q_INVOKABLE QString saveAsFolder() const;
    // (A GIF Pin: Copy puts the GIF file on the clipboard, Save as writes a .gif.)
    Q_INVOKABLE QString saveAsSuggestion(int pin) const;
    Q_INVOKABLE int saveAsFilterIndex() const;
    // Edit: the Pin closes and its image comes back as a Thumbnail with the Editor open on it.
    Q_INVOKABLE void edit(int pin); // a GIF Pin has no Edit: nothing happens
    // Locked: the Pin's window takes no focus and Hyprland's hit-testing skips it, so
    // input reaches what is beneath. False (and still unlocked/locked as before) if
    // Hyprland could not be asked.
    Q_INVOKABLE bool lock(int pin);
    Q_INVOKABLE bool unlock(int pin);
    Q_INVOKABLE bool isLocked(int pin) const;
    // Any Pin unlocked: lock them all. All locked: unlock them all.
    bool lockToggle();
    Q_INVOKABLE void close(int pin);
    Q_INVOKABLE void closeAll();

signals:
    // A new Pin: the UI puts a window on it (the opening rule is already in place).
    void opened(int pin);
    void closed(int pin);
    void opacityChanged(int pin);
    void lockedChanged(int pin);

private:
    bool setLocked(int pin, bool locked);
    struct Pin {
        ThumbnailStack::Capture capture;
        QImage image;
        QRect opening;    // where and how big it opened: the aspect and the scale limits come from it
        QRect rect;       // where it was last known to be
        qreal opacity = 1;
        bool locked = false;
        QString gifPath;  // a GIF Pin: its own copy of the GIF; empty for an image Pin
        bool resizePending = false; // notches are being gathered; applyResize is due
        bool snapPending = false;
        qreal wantedWidth = 0;      // the width those notches add up to
    };
    int pinGif(int recordingId);
    static QString gifCacheDir();
    void applyResize(int pin);
    void snapToShape(int pin);
    static QSize shapedSize(const Pin &p, qreal width);
    static bool isOnShape(const Pin &p, const QSize &size);
    Pin *find(int pin);
    const Pin *find(int pin) const;
    ThumbnailStack &stack();

    Daemon &m_daemon;
    std::map<int, Pin> m_pins;
    int m_nextId = 1;
};

} // namespace tarn
