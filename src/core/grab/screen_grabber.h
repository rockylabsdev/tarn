#pragma once
// The Screen grabber: wraps grim.
#include <QImage>
#include <QObject>

namespace tarn {

class Daemon;

class ScreenGrabber : public QObject {
    Q_OBJECT
public:
    explicit ScreenGrabber(Daemon &daemon) : m_daemon(daemon) {}

    // Grabs one whole monitor, full resolution, pointer left out (see
    // README: cursor:no_hardware_cursors = 0). grim sends it uncompressed
    // (-t ppm): its PNG took ~400 ms of a 2560x1440 shot, the PPM ~35 ms, and the
    // Thumbnails are off screen for that long. Whoever keeps the shot makes the
    // PNG off the daemon's thread (encodePngInBackground). Returns the pixels, or
    // a null image with *error set.
    QImage grabMonitor(const QString &monitorName, QString *error);
    // A grab is running. It lets the event loop run (an Overlay or the Thumbnails
    // leaving the screen), so a second key press can arrive meanwhile: the capture
    // commands do nothing while busy, rather than take a second shot.
    bool busy() const { return m_busy; }

private:
    Daemon &m_daemon;
    bool m_busy = false;
};

} // namespace tarn
