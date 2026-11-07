#pragma once
// Raises desktop notifications through notify-send.
#include <QObject>

namespace tarn {

class Daemon;

class Notifier : public QObject {
    Q_OBJECT
public:
    explicit Notifier(Daemon &daemon) : m_daemon(daemon) {}
    void notify(const QString &summary, const QString &body = {});

private:
    Daemon &m_daemon;
};

} // namespace tarn
