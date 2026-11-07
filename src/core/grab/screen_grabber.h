#pragma once
// The Screen grabber: wraps grim.
#include <QByteArray>
#include <QObject>

namespace tarn {

class Daemon;

class ScreenGrabber : public QObject {
    Q_OBJECT
public:
    explicit ScreenGrabber(Daemon &daemon) : m_daemon(daemon) {}

    // Grabs one whole monitor, full resolution, pointer left out (see
    // README: cursor:no_hardware_cursors = 0). Returns grim's PNG bytes
    // untouched, or empty with *error set.
    QByteArray grabMonitor(const QString &monitorName, QString *error);

private:
    Daemon &m_daemon;
};

} // namespace tarn
